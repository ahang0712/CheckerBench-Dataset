# Patch-overlapping Python context before the fix

## `pytorch_lightning/utilities/argparse.py` — `function:parse_env_variables` (lines 99-126)

```python
def parse_env_variables(cls: Type["pl.Trainer"], template: str = "PL_%(cls_name)s_%(cls_argument)s") -> Namespace:
    """Parse environment arguments if they are defined.

    Examples:

        >>> from pytorch_lightning import Trainer
        >>> parse_env_variables(Trainer)
        Namespace()
        >>> import os
        >>> os.environ["PL_TRAINER_GPUS"] = '42'
        >>> os.environ["PL_TRAINER_BLABLABLA"] = '1.23'
        >>> parse_env_variables(Trainer)
        Namespace(gpus=42)
        >>> del os.environ["PL_TRAINER_GPUS"]
    """
    cls_arg_defaults = get_init_arguments_and_types(cls)

    env_args = {}
    for arg_name, _, _ in cls_arg_defaults:
        env = template % {"cls_name": cls.__name__.upper(), "cls_argument": arg_name.upper()}
        val = os.environ.get(env)
        if not (val is None or val == ""):
            # todo: specify the possible exception
            with suppress(Exception):
                # converting to native types like int/float/bool
                val = eval(val)
            env_args[arg_name] = val
    return Namespace(**env_args)
```

## `pytorch_lightning/utilities/argparse.py` — `module:<module>@20` (lines 20-20)

```python
from contextlib import suppress
```
