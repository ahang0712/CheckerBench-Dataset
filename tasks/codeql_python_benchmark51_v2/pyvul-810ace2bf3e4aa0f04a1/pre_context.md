# Patch-overlapping Python context before the fix

## `weixin/msg.py` — `class:WeixinMsg` (lines 40-336)

```python
class WeixinMsg(object):

    def __init__(self, token, sender=None, expires_in=0):
        self.token = token
        self.sender = sender
        self.expires_in = expires_in
        self._registry = dict()

    def validate(self, signature, timestamp, nonce):
        if not self.token:
            raise WeixinMsgError("weixin token is missing")

        if self.expires_in:
            try:
                timestamp = int(timestamp)
            except ValueError:
                return False
            delta = time.time() - timestamp
            if delta < 0 or delta > self.expires_in:
                return False
        values = [self.token, str(timestamp), str(nonce)]
        s = ''.join(sorted(values))
        hsh = hashlib.sha1(s.encode("utf-8")).hexdigest()
        return signature == hsh

    def parse(self, content):
        raw = {}
        root = etree.fromstring(content)
        for child in root:
            raw[child.tag] = child.text

        formatted = self.format(raw)
        msg_type = formatted['type']
        msg_parser = getattr(self, 'parse_{0}'.format(msg_type), None)
        if callable(msg_parser):
            parsed = msg_parser(raw)
        else:
            parsed = self.parse_invalid_type(raw)

        formatted.update(parsed)
        return formatted

    def format(self, kwargs):
        timestamp = int(kwargs['CreateTime'])
        return {
            'id': kwargs.get('MsgId'),
            'timestamp': timestamp,
            'receiver': kwargs['ToUserName'],
            'sender': kwargs['FromUserName'],
            'type': kwargs['MsgType'],
            'time': datetime.fromtimestamp(timestamp),
        }

    def parse_text(self, raw):
        return {'content': raw['Content']}

    def parse_image(self, raw):
        return {'picurl': raw['PicUrl']}

    def parse_location(self, raw):
        return {
            'location_x': raw['Location_X'],
            'location_y': raw['Location_Y'],
            'scale': int(raw.get('Scale', 0)),
            'label': raw['Label'],
        }

    def parse_link(self, raw):
        return {
            'title': raw['Title'],
            'description': raw['Description'],
            'url': raw['url'],
        }

    def parse_voice(self, raw):
        return {
            'media_id': raw['MediaId'],
            'format': raw['Format'],
            'recognition': raw['Recognition'],
        }

    def parse_video(self, raw):
        return {
            'media_id': raw['MediaId'],
            'thumb_media_id': raw['ThumbMediaId'],
        }

    def parse_shortvideo(self, raw):
        return {
            'media_id': raw['MediaId'],
            'thumb_media_id': raw['ThumbMediaId'],
        }

    def parse_event(self, raw):
        return {
            'event': raw.get('Event'),
            'event_key': raw.get('EventKey'),
            'ticket': raw.get('Ticket'),
            'latitude': raw.get('Latitude'),
            'longitude': raw.get('Longitude'),
            'precision': raw.get('Precision'),
            'status': raw.get('status')
        }

    def parse_invalid_type(self, raw):
        return {}

    def reply(self, username=None, type='text', sender=None, **kwargs):
        if not username:
            raise RuntimeError("username is missing")
        sender = sender or self.sender
        if not sender:
            raise RuntimeError('WEIXIN_SENDER or sender argument is missing')

        if type == 'text':
            content = kwargs.get('content', '')
            return text_reply(username, sender, content)

        if type == 'music':
            values = {}
            for k in ('title', 'description', 'music_url', 'hq_music_url'):
                values[k] = kwargs[k]
            return music_reply(username, sender, **values)

        if type == 'news':
            items = kwargs['articles']
            return news_reply(username, sender, *items)

        if type == 'customer_service':
            service_account = kwargs['service_account']
            return transfer_customer_service_reply(username, sender,
                                                   service_account)

        if type == 'image':
            media_id = kwargs.get('media_id')
            return image_reply(username, sender, media_id)

        if type == 'voice':
            media_id = kwargs.get('media_id')
            return voice_reply(username, sender, media_id)

        if type == 'video':
            values = {}
            for k in ('media_id', 'title', 'description'):
                values[k] = kwargs[k]
            return video_reply(username, sender, **values)

    def register(self, type, key=None, func=None):
        if func:
            key = '*' if not key else key
            self._registry.setdefault(type, dict())[key] = func
            return func
        return self.__call__(type, key)

    def __call__(self, type, key):
        def wrapper(func):
            self.register(type, key, func)
            return func
        return wrapper

    @property
    def all(self):
        return self.register('*')

    def text(self, key='*'):
        return self.register('text', key)

    def __getattr__(self, key):
        key = key.lower()
        if key in ['image', 'video', 'voice', 'shortvideo', 'location', 'link', 'event']:
            return self.register(key)
        if key in ['subscribe', 'unsubscribe', 'location', 'click', 'view', 'scan', \
                   'scancode_push', 'scancode_waitmsg', 'pic_sysphoto', \
                   'pic_photo_or_album', 'pic_weixin', 'location_select', \
                   'qualification_verify_success', 'qualification_verify_fail', 'naming_verify_success', \
                   'naming_verify_fail', 'annual_renew', 'verify_expired', \
                   'card_pass_check', 'user_get_card', 'user_del_card', 'user_consume_card', \
                   'user_pay_from_pay_cell', 'user_view_card', 'user_enter_session_from_card', \
                   'card_sku_remind']:
            return self.register('event', key)
        raise AttributeError('invalid attribute "' + key + '"')

    def django_view_func(self):

        def run(request):
            if HttpResponse is None:
                raise RuntimeError('django_view_func need Django be installed')
            signature = request.GET.get('signature')

            timestamp = request.GET.get('timestamp')
            nonce = request.GET.get('nonce')
            if not self.validate(signature, timestamp, nonce):
                return HttpResponseForbidden('signature failed')
            if request.method == 'GET':
                echostr = request.args.get('echostr', '')
                return HttpResponse(echostr)
            elif request.method == "POST":
                try:
                    ret = self.parse(request.body)
                except ValueError:
                    return HttpResponseForbidden('invalid')

                func = None
                type = ret['type']
                _registry = self._registry.get(type, dict())
                if type == 'text':
                    if ret['content'] in _registry:
                        func = _registry[ret['content']]
                elif type == 'event':
                    if ret['event'].lower() in _registry:
                        func = _registry[ret['event'].lower()]

                if func is None and '*' in _registry:
                    func = _registry['*']
                if func is None and '*' in self._registry:
                    func = self._registry.get('*', dict()).get('*')

                text = ''
                if func is None:
                    text = 'failed'

                if callable(func):
                    text = func(**ret)

                content = ''
                if isinstance(text, basestring):
                    if text:
                        content = self.reply(
                            username=ret['sender'],
                            sender=ret['receiver'],
                            content=text,
                        )
                elif isinstance(text, dict):
                    text.setdefault('username', ret['sender'])
                    text.setdefault('sender', ret['receiver'])
                    content = self.reply(**text)

                return HttpResponse(content, content_type='text/xml; charset=utf-8')
            return HttpResponseNotAllowed(['GET', 'POST'])
        return run

    def view_func(self):
        if request is None:
            raise RuntimeError('view_func need Flask be installed')

        signature = request.args.get('signature')
        timestamp = request.args.get('timestamp')
        nonce = request.args.get('nonce')
        if not self.validate(signature, timestamp, nonce):
            return 'signature failed', 400
        if request.method == 'GET':
            echostr = request.args.get('echostr', '')
            return echostr

        try:
            ret = self.parse(request.data)
        except ValueError:
            return 'invalid', 400

        func = None
        type = ret['type']
        _registry = self._registry.get(type, dict())
        if type == 'text':
            if ret['content'] in _registry:
                func = _registry[ret['content']]
        elif type == 'event':
            if ret['event'].lower() in _registry:
                func = _registry[ret['event'].lower()]

        if func is None and '*' in _registry:
            func = _registry['*']
        if func is None and '*' in self._registry:
            func = self._registry.get('*', dict()).get('*')

        text = ''
        if func is None:
            text = 'failed'

        if callable(func):
            text = func(**ret)

        content = ''
        if isinstance(text, basestring):
            if text:
                content = self.reply(
                    username=ret['sender'],
                    sender=ret['receiver'],
                    content=text,
                )
        elif isinstance(text, dict):
            text.setdefault('username', ret['sender'])
            text.setdefault('sender', ret['receiver'])
            content = self.reply(**text)

        return Response(content, content_type='text/xml; charset=utf-8')

    view_func.methods = ['GET', 'POST']
```

## `weixin/msg.py` — `function:WeixinMsg.parse` (lines 65-80)

```python
    def parse(self, content):
        raw = {}
        root = etree.fromstring(content)
        for child in root:
            raw[child.tag] = child.text

        formatted = self.format(raw)
        msg_type = formatted['type']
        msg_parser = getattr(self, 'parse_{0}'.format(msg_type), None)
        if callable(msg_parser):
            parsed = msg_parser(raw)
        else:
            parsed = self.parse_invalid_type(raw)

        formatted.update(parsed)
        return formatted
```

## `weixin/pay.py` — `class:WeixinPay` (lines 40-233)

```python
class WeixinPay(object):

    def __init__(self, app_id, mch_id, mch_key, notify_url, key=None, cert=None):
        self.app_id = app_id
        self.mch_id = mch_id
        self.mch_key = mch_key
        self.notify_url = notify_url
        self.key = key
        self.cert = cert
        self.sess = requests.Session()

    @property
    def remote_addr(self):
        if request is not None:
            return request.remote_addr
        return ""

    @property
    def nonce_str(self):
        char = string.ascii_letters + string.digits
        return "".join(random.choice(char) for _ in range(32))

    def sign(self, raw):
        raw = [(k, str(raw[k]) if isinstance(raw[k], int) else raw[k])
               for k in sorted(raw.keys())]
        s = "&".join("=".join(kv) for kv in raw if kv[1])
        s += "&key={0}".format(self.mch_key)
        return hashlib.md5(s.encode("utf-8")).hexdigest().upper()

    def check(self, data):
        sign = data.pop("sign")
        return sign == self.sign(data)

    def to_xml(self, raw):
        s = ""
        for k, v in raw.items():
            s += "<{0}>{1}</{0}>".format(k, v)
        s = "<xml>{0}</xml>".format(s)
        return s.encode("utf-8")

    def to_dict(self, content):
        raw = {}
        root = etree.fromstring(content.encode("utf-8"))
        for child in root:
            raw[child.tag] = child.text
        return raw

    def _fetch(self, url, data, use_cert=False):
        data.setdefault("appid", self.app_id)
        data.setdefault("mch_id", self.mch_id)
        data.setdefault("nonce_str", self.nonce_str)
        data.setdefault("sign", self.sign(data))

        if use_cert:
            resp = self.sess.post(url, data=self.to_xml(data), cert=(self.cert, self.key))
        else:
            resp = self.sess.post(url, data=self.to_xml(data))
        content = resp.content.decode("utf-8")
        if "return_code" in content:
            data = Map(self.to_dict(content))
            if data.return_code == FAIL:
                raise WeixinPayError(data.return_msg)
            if "result_code" in content and data.result_code == FAIL:
                raise WeixinPayError(data.err_code_des)
            return data
        return content

    def reply(self, msg, ok=True):
        code = SUCCESS if ok else FAIL
        return self.to_xml(dict(return_code=code, return_msg=msg))

    def unified_order(self, **data):
        """
        统一下单
        out_trade_no、body、total_fee、trade_type必填
        app_id, mchid, nonce_str自动填写
        spbill_create_ip 在flask框架下可以自动填写, 非flask框架需要主动传入此参数
        """
        url = "https://api.mch.weixin.qq.com/pay/unifiedorder"

        # 必填参数
        if "out_trade_no" not in data:
            raise WeixinPayError("缺少统一支付接口必填参数out_trade_no")
        if "body" not in data:
            raise WeixinPayError("缺少统一支付接口必填参数body")
        if "total_fee" not in data:
            raise WeixinPayError("缺少统一支付接口必填参数total_fee")
        if "trade_type" not in data:
            raise WeixinPayError("缺少统一支付接口必填参数trade_type")

        # 关联参数
        if data["trade_type"] == "JSAPI" and "openid" not in data:
            raise WeixinPayError("trade_type为JSAPI时，openid为必填参数")
        if data["trade_type"] == "NATIVE" and "product_id" not in data:
            raise WeixinPayError("trade_type为NATIVE时，product_id为必填参数")
        data.setdefault("notify_url", self.notify_url)
        if "spbill_create_ip" not in data:
            data.setdefault("spbill_create_ip", self.remote_addr)

        raw = self._fetch(url, data)
        return raw

    def jsapi(self, **kwargs):
        """
        生成给JavaScript调用的数据
        详细规则参考 https://pay.weixin.qq.com/wiki/doc/api/jsapi.php?chapter=7_7&index=6
        """
        kwargs.setdefault("trade_type", "JSAPI")
        raw = self.unified_order(**kwargs)
        package = "prepay_id={0}".format(raw["prepay_id"])
        timestamp = str(int(time.time()))
        nonce_str = self.nonce_str
        raw = dict(appId=self.app_id, timeStamp=timestamp,
                   nonceStr=nonce_str, package=package, signType="MD5")
        sign = self.sign(raw)
        return dict(package=package, appId=self.app_id, signType="MD5",
                    timeStamp=timestamp, nonceStr=nonce_str, sign=sign)

    def order_query(self, **data):
        """
        订单查询
        out_trade_no, transaction_id至少填一个
        appid, mchid, nonce_str不需要填入
        """
        url = "https://api.mch.weixin.qq.com/pay/orderquery"

        if "out_trade_no" not in data and "transaction_id" not in data:
            raise WeixinPayError("订单查询接口中，out_trade_no、transaction_id至少填一个")

        return self._fetch(url, data)

    def close_order(self, out_trade_no, **data):
        """
        关闭订单
        out_trade_no必填
        appid, mchid, nonce_str不需要填入
        """
        url = "https://api.mch.weixin.qq.com/pay/closeorder"

        data.setdefault("out_trade_no", out_trade_no)

        return self._fetch(url, data)

    def refund(self, **data):
        """
        申请退款
        out_trade_no、transaction_id至少填一个且
        out_refund_no、total_fee、refund_fee、op_user_id为必填参数
        appid、mchid、nonce_str不需要填入
        """
        if not self.key or not self.cert:
            raise WeixinError("退款申请接口需要双向证书")
        url = "https://api.mch.weixin.qq.com/secapi/pay/refund"
        if "out_trade_no" not in data and "transaction_id" not in data:
            raise WeixinPayError("退款申请接口中，out_trade_no、transaction_id至少填一个")
        if "out_refund_no" not in data:
            raise WeixinPayError("退款申请接口中，缺少必填参数out_refund_no");
        if "total_fee" not in data:
            raise WeixinPayError("退款申请接口中，缺少必填参数total_fee");
        if "refund_fee" not in data:
            raise WeixinPayError("退款申请接口中，缺少必填参数refund_fee");

        return self._fetch(url, data, True)

    def refund_query(self, **data):
        """
        查询退款
        提交退款申请后，通过调用该接口查询退款状态。退款有一定延时，
        用零钱支付的退款20分钟内到账，银行卡支付的退款3个工作日后重新查询退款状态。

        out_refund_no、out_trade_no、transaction_id、refund_id四个参数必填一个
        appid、mchid、nonce_str不需要填入
        """
        url = "https://api.mch.weixin.qq.com/pay/refundquery"
        if "out_refund_no" not in data and "out_trade_no" not in data \
                and "transaction_id" not in data and "refund_id" not in data:
            raise WeixinPayError("退款查询接口中，out_refund_no、out_trade_no、transaction_id、refund_id四个参数必填一个")

        return self._fetch(url, data)

    def download_bill(self, bill_date, bill_type="ALL", **data):
        """
        下载对账单
        bill_date、bill_type为必填参数
        appid、mchid、nonce_str不需要填入
        """
        url = "https://api.mch.weixin.qq.com/pay/downloadbill"
        data.setdefault("bill_date", bill_date)
        data.setdefault("bill_type", bill_type)

        if "bill_date" not in data:
            raise WeixinPayError("对账单接口中，缺少必填参数bill_date")

        return self._fetch(url, data)
```

## `weixin/pay.py` — `function:WeixinPay.to_dict` (lines 80-85)

```python
    def to_dict(self, content):
        raw = {}
        root = etree.fromstring(content.encode("utf-8"))
        for child in root:
            raw[child.tag] = child.text
        return raw
```
