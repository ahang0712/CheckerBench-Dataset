# Patch-overlapping Python context before the fix

## `tests/parser/functions/test_raw_call.py` — `function:test_checkable_raw_call` (lines 292-345)

```python
def test_checkable_raw_call(get_contract, assert_tx_failed):
    target_source = """
baz: int128
@external
def fail1(should_raise: bool):
    if should_raise:
        raise "fail"
# test both paths for raw_call -
# they are different depending if callee has or doesn't have returntype
@external
def fail2(should_raise: bool) -> int128:
    if should_raise:
        self.baz = self.baz + 1
    return self.baz
"""

    caller_source = """
@external
@view
def foo(_addr: address, should_raise: bool) -> uint256:
    success: bool = True
    response: Bytes[32] = b""
    success, response = raw_call(
        _addr,
        _abi_encode(should_raise, method_id=method_id("fail1(bool)")),
        max_outsize=32,
        is_static_call=True,
        revert_on_failure=False,
    )
    assert success == (not should_raise)
    return 1
@external
@view
def bar(_addr: address, should_raise: bool) -> uint256:
    success: bool = True
    response: Bytes[32] = b""
    success, response = raw_call(
        _addr,
        _abi_encode(should_raise, method_id=method_id("fail2(bool)")),
        max_outsize=32,
        is_static_call=True,
        revert_on_failure=False,
    )
    assert success == (not should_raise)
    return 2
    """

    target = get_contract(target_source)
    caller = get_contract(caller_source)

    assert caller.foo(target.address, True) == 1
    assert caller.foo(target.address, False) == 1
    assert caller.bar(target.address, True) == 2
    assert caller.bar(target.address, False) == 2
```

## `vyper/builtins/functions.py` — `class:RawCall` (lines 1067-1210)

```python
class RawCall(BuiltinFunction):
    _id = "raw_call"
    _inputs = [("to", AddressT()), ("data", BytesT.any())]
    _kwargs = {
        "max_outsize": KwargSettings(UINT256_T, 0, require_literal=True),
        "gas": KwargSettings(UINT256_T, "gas"),
        "value": KwargSettings(UINT256_T, zero_value),
        "is_delegate_call": KwargSettings(BoolT(), False, require_literal=True),
        "is_static_call": KwargSettings(BoolT(), False, require_literal=True),
        "revert_on_failure": KwargSettings(BoolT(), True, require_literal=True),
    }
    _return_type = None

    def fetch_call_return(self, node):
        self._validate_arg_types(node)

        kwargz = {i.arg: i.value for i in node.keywords}

        outsize = kwargz.get("max_outsize")
        revert_on_failure = kwargz.get("revert_on_failure")
        revert_on_failure = revert_on_failure.value if revert_on_failure is not None else True

        if outsize is None:
            if revert_on_failure:
                return None
            return BoolT()

        if not isinstance(outsize, vy_ast.Int) or outsize.value < 0:
            raise

        if outsize.value:
            return_type = BytesT()
            return_type.set_min_length(outsize.value)

            if revert_on_failure:
                return return_type
            return TupleT([BoolT(), return_type])

    def infer_arg_types(self, node):
        self._validate_arg_types(node)
        # return a concrete type for `data`
        data_type = get_possible_types_from_node(node.args[1]).pop()
        return [self._inputs[0][1], data_type]

    @process_inputs
    def build_IR(self, expr, args, kwargs, context):
        to, data = args
        # TODO: must compile in source code order, left-to-right
        gas, value, outsize, delegate_call, static_call, revert_on_failure = (
            kwargs["gas"],
            kwargs["value"],
            kwargs["max_outsize"],
            kwargs["is_delegate_call"],
            kwargs["is_static_call"],
            kwargs["revert_on_failure"],
        )

        if delegate_call and static_call:
            raise ArgumentException(
                "Call may use one of `is_delegate_call` or `is_static_call`, not both", expr
            )
        if not static_call and context.is_constant():
            raise StateAccessViolation(
                f"Cannot make modifying calls from {context.pp_constancy()},"
                " use `is_static_call=True` to perform this action",
                expr,
            )

        if data.value == "~calldata":
            call_ir = ["with", "mem_ofst", "msize"]
            args_ofst = ["seq", ["calldatacopy", "mem_ofst", 0, "calldatasize"], "mem_ofst"]
            args_len = "calldatasize"
        else:
            # some gymnastics to propagate constants (if eval_input_buf
            # returns a static memory location)
            eval_input_buf = ensure_in_memory(data, context)

            input_buf = eval_seq(eval_input_buf)

            if input_buf is None:
                call_ir = ["with", "arg_buf", eval_input_buf]
                input_buf = IRnode.from_list("arg_buf")
            else:
                call_ir = ["seq", eval_input_buf]

            args_ofst = add_ofst(input_buf, 32)
            args_len = ["mload", input_buf]

        output_node = IRnode.from_list(
            context.new_internal_variable(BytesT(outsize)), typ=BytesT(outsize), location=MEMORY
        )

        bool_ty = BoolT()

        # build IR for call or delegatecall
        common_call_args = [
            args_ofst,
            args_len,
            # if there is no return value, the return offset can be 0
            add_ofst(output_node, 32) if outsize else 0,
            outsize,
        ]

        if delegate_call:
            call_op = ["delegatecall", gas, to, *common_call_args]
        elif static_call:
            call_op = ["staticcall", gas, to, *common_call_args]
        else:
            call_op = ["call", gas, to, value, *common_call_args]

        call_ir += [call_op]

        # build sequence IR
        if outsize:
            # return minimum of outsize and returndatasize
            size = ["select", ["lt", outsize, "returndatasize"], outsize, "returndatasize"]

            # store output size and return output location
            store_output_size = ["seq", ["mstore", output_node, size], output_node]

            bytes_ty = BytesT(outsize)

            if revert_on_failure:
                typ = bytes_ty
                ret_ir = ["seq", check_external_call(call_ir), store_output_size]
            else:
                typ = TupleT([bool_ty, bytes_ty])
                ret_ir = [
                    "multi",
                    # use IRnode.from_list to make sure the types are
                    # set properly on the "multi" members
                    IRnode.from_list(call_ir, typ=bool_ty),
                    IRnode.from_list(store_output_size, typ=bytes_ty, location=MEMORY),
                ]

        else:
            if revert_on_failure:
                typ = None
                ret_ir = check_external_call(call_ir)
            else:
                typ = bool_ty
                ret_ir = call_ir

        return IRnode.from_list(ret_ir, typ=typ, location=MEMORY)
```

## `vyper/builtins/functions.py` — `function:RawCall.build_IR` (lines 1111-1210)

```python
    def build_IR(self, expr, args, kwargs, context):
        to, data = args
        # TODO: must compile in source code order, left-to-right
        gas, value, outsize, delegate_call, static_call, revert_on_failure = (
            kwargs["gas"],
            kwargs["value"],
            kwargs["max_outsize"],
            kwargs["is_delegate_call"],
            kwargs["is_static_call"],
            kwargs["revert_on_failure"],
        )

        if delegate_call and static_call:
            raise ArgumentException(
                "Call may use one of `is_delegate_call` or `is_static_call`, not both", expr
            )
        if not static_call and context.is_constant():
            raise StateAccessViolation(
                f"Cannot make modifying calls from {context.pp_constancy()},"
                " use `is_static_call=True` to perform this action",
                expr,
            )

        if data.value == "~calldata":
            call_ir = ["with", "mem_ofst", "msize"]
            args_ofst = ["seq", ["calldatacopy", "mem_ofst", 0, "calldatasize"], "mem_ofst"]
            args_len = "calldatasize"
        else:
            # some gymnastics to propagate constants (if eval_input_buf
            # returns a static memory location)
            eval_input_buf = ensure_in_memory(data, context)

            input_buf = eval_seq(eval_input_buf)

            if input_buf is None:
                call_ir = ["with", "arg_buf", eval_input_buf]
                input_buf = IRnode.from_list("arg_buf")
            else:
                call_ir = ["seq", eval_input_buf]

            args_ofst = add_ofst(input_buf, 32)
            args_len = ["mload", input_buf]

        output_node = IRnode.from_list(
            context.new_internal_variable(BytesT(outsize)), typ=BytesT(outsize), location=MEMORY
        )

        bool_ty = BoolT()

        # build IR for call or delegatecall
        common_call_args = [
            args_ofst,
            args_len,
            # if there is no return value, the return offset can be 0
            add_ofst(output_node, 32) if outsize else 0,
            outsize,
        ]

        if delegate_call:
            call_op = ["delegatecall", gas, to, *common_call_args]
        elif static_call:
            call_op = ["staticcall", gas, to, *common_call_args]
        else:
            call_op = ["call", gas, to, value, *common_call_args]

        call_ir += [call_op]

        # build sequence IR
        if outsize:
            # return minimum of outsize and returndatasize
            size = ["select", ["lt", outsize, "returndatasize"], outsize, "returndatasize"]

            # store output size and return output location
            store_output_size = ["seq", ["mstore", output_node, size], output_node]

            bytes_ty = BytesT(outsize)

            if revert_on_failure:
                typ = bytes_ty
                ret_ir = ["seq", check_external_call(call_ir), store_output_size]
            else:
                typ = TupleT([bool_ty, bytes_ty])
                ret_ir = [
                    "multi",
                    # use IRnode.from_list to make sure the types are
                    # set properly on the "multi" members
                    IRnode.from_list(call_ir, typ=bool_ty),
                    IRnode.from_list(store_output_size, typ=bytes_ty, location=MEMORY),
                ]

        else:
            if revert_on_failure:
                typ = None
                ret_ir = check_external_call(call_ir)
            else:
                typ = bool_ty
                ret_ir = call_ir

        return IRnode.from_list(ret_ir, typ=typ, location=MEMORY)
```
