# Patch-overlapping Go context before the fix

## `modules/zerogod/zerogod_acceptor.go` (changed lines (120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130))

```go
package zerogod

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"strings"

	"github.com/evilsocket/islazy/ops"
	"github.com/evilsocket/islazy/tui"
)

type Handler struct {
	TLS    bool
	Handle func(ctx *HandlerContext)
}

// TODO: possibly autodetect from peeking at the first bytes sent by the client
var TCP_HANDLERS = map[string]Handler{
	"_ipp": {
		Handle: ippClientHandler,
	},
	"_ipps": {
		TLS:    true,
		Handle: ippClientHandler,
	},
	"_http": {
		Handle: httpClientHandler,
	},
	"_https": {
		TLS:    true,
		Handle: httpClientHandler,
	},
}

type Acceptor struct {
	mod           *ZeroGod
	srvHost       string
	proto         string
	port          uint16
	service       string
	tlsConfig     *tls.Config
	tcpListener   net.Listener
	udpListener   *net.UDPConn
	running       bool
	context       context.Context
	ctxCancel     context.CancelFunc
	handler       Handler
	ippAttributes map[string]string
	httpPaths     map[string]string
}

type HandlerContext struct {
	service       string
	mod           *ZeroGod
	client        net.Conn
	srvHost       string
	srvPort       int
	srvTLS        bool
	ippAttributes map[string]string
	httpPaths     map[string]string
}

func NewAcceptor(mod *ZeroGod, service string, srvHost string, port uint16, tlsConfig *tls.Config, ippAttributes map[string]string, httpPaths map[string]string) *Acceptor {
	context, ctcCancel := context.WithCancel(context.Background())
	proto := ops.Ternary(strings.Contains(service, "_tcp"), "tcp", "udp").(string)

	acceptor := Acceptor{
		mod:           mod,
		port:          port,
		proto:         proto,
		service:       service,
		context:       context,
		ctxCancel:     ctcCancel,
		srvHost:       srvHost,
		ippAttributes: ippAttributes,
		httpPaths:     httpPaths,
	}

	for svcName, svcHandler := range TCP_HANDLERS {
		if strings.Contains(service, svcName) {
			if svcHandler.TLS {
				acceptor.tlsConfig = tlsConfig
			}
			acceptor.handler = svcHandler
			break
		}
	}

	if acceptor.handler.Handle == nil {
		mod.Warning("no protocol handler found for service %s, using generic %s dump handler", tui.Yellow(service), proto)
		acceptor.handler.Handle = handleGenericTCP
	} else {
		mod.Info("found %s %s protocol handler (tls=%v)", proto, tui.Green(service), acceptor.tlsConfig != nil)
	}

	return &acceptor
}

func (a *Acceptor) startTCP() (err error) {
	var lc net.ListenConfig
	if a.tcpListener, err = lc.Listen(a.context, "tcp", fmt.Sprintf("0.0.0.0:%d", a.port)); err != nil {
		return err
	}
	if a.tlsConfig != nil {
		a.tcpListener = tls.NewListener(a.tcpListener, a.tlsConfig)
	}

	a.running = true
	go func() {
		a.mod.Debug("%s listener for port %d (%s) started", a.proto, a.port, tui.Green(a.service))
		for a.running {
			if conn, err := a.tcpListener.Accept(); err != nil {
				if a.running {
					a.mod.Error("%v", err)
				}
			} else {
				a.mod.Debug("accepted %s connection for service %s (port %d): %v", a.proto, tui.Green(a.service), a.port, conn.RemoteAddr())
				go a.handler.Handle(&HandlerContext{
					service:       a.service,
					mod:           a.mod,
					client:        conn,
					srvHost:       a.srvHost,
					srvPort:       int(a.port),
					srvTLS:        a.tlsConfig != nil,
					ippAttributes: a.ippAttributes,
					httpPaths:     a.httpPaths,
				})
			}
		}
		a.mod.Debug("%s listener for port %d (%s) stopped", a.proto, a.port, tui.Green(a.service))
	}()

	return nil
}

func (a *Acceptor) startUDP() (err error) {
	if udpAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("0.0.0.0:%d", a.port)); err != nil {
		return err
	} else if a.udpListener, err = net.ListenUDP("udp", udpAddr); err != nil {
		return err
	} else {
		a.running = true
		go func() {
			var buffer [4096]byte

			a.mod.Info("%s listener for port %d (%s) started", a.proto, a.port, tui.Green(a.service))

			for a.running {
				if n, addr, err := a.udpListener.ReadFromUDP(buffer[0:]); err != nil {
					a.mod.Warning("error reading udp packet: %v", err)
				} else if n <= 0 {
					a.mod.Info("empty read")
				} else {
					a.mod.Info("%v:\n%s", addr, Dump(buffer[0:n]))
				}
			}

			a.mod.Info("%s listener for port %d (%s) stopped", a.proto, a.port, tui.Green(a.service))
		}()
	}

	return nil
}

func (a *Acceptor) Start() (err error) {
	if a.proto == "tcp" {
		return a.startTCP()
	} else {
		return a.startUDP()
	}
}

func (a *Acceptor) Stop() {
	a.mod.Debug("stopping %s listener for port %d", a.proto, a.port)
	a.running = false

	if a.proto == "tcp" {
		a.ctxCancel()
		<-a.context.Done()
		a.tcpListener.Close()
	} else {
		a.udpListener.Close()
	}

	a.mod.Debug("%s listener for port %d stopped", a.proto, a.port)
}
```

## `modules/zerogod/zerogod_ipp_primitives.go` (changed lines (16, 139))

```go
package zerogod

import (
	"bytes"
	"fmt"
	"io"
	"net/http"
	"slices"
	"strconv"

	"github.com/evilsocket/islazy/str"
	"github.com/phin1x/go-ipp"
)

const IPP_CHUNK_MAX_LINE_SIZE = 1024

var IPP_REQUEST_NAMES = map[int16]string{
	// https://tools.ietf.org/html/rfc2911#section-4.4.15
	0x0002: "Print-Job",
	0x0003: "Print-URI",
	0x0004: "Validate-Job",
	0x0005: "Create-Job",
	0x0006: "Send-Document",
	0x0007: "Send-URI",
	0x0008: "Cancel-Job",
	0x0009: "Get-Job-Attributes",
	0x000A: "Get-Jobs",
	0x000B: "Get-Printer-Attributes",
	0x000C: "Hold-Job",
	0x000D: "Release-Job",
	0x000E: "Restart-Job",
	0x0010: "Pause-Printer",
	0x0011: "Resume-Printer",
	0x0012: "Purge-Jobs",
	// https://web.archive.org/web/20061024184939/http://uw714doc.sco.com/en/cups/ipp.html
	0x4001: "CUPS-Get-Default",
	0x4002: "CUPS-Get-Printers",
	0x4003: "CUPS-Add-Modify-Printer",
	0x4004: "CUPS-Delete-Printer",
	0x4005: "CUPS-Get-Classes",
	0x4006: "CUPS-Add-Modify-Class",
	0x4007: "CUPS-Delete-Class",
	0x4008: "CUPS-Accept-Jobs",
	0x4009: "CUPS-Reject-Jobs",
	0x400A: "CUPS-Set-Default",
	0x400B: "CUPS-Get-Devices",
	0x400C: "CUPS-Get-PPDs",
	0x400D: "CUPS-Move-Job",
}

var IPP_USER_ATTRIBUTES = map[string]string{
	"printer-name":               "PRINTER_NAME",
	"printer-info":               "PRINTER_INFO",
	"printer-make-and-model":     "PRINTER_MAKE PRINTER_MODEL",
	"printer-location":           "PRINTER_LOCATION",
	"printer-privacy-policy-uri": "https://www.bettercap.org/",
	"ppd-name":                   "everywhere",
}

func init() {
	ipp.AttributeTagMapping["printer-uri-supported"] = ipp.TagUri
	ipp.AttributeTagMapping["uri-authentication-supported"] = ipp.TagKeyword
	ipp.AttributeTagMapping["uri-security-supported"] = ipp.TagKeyword
	ipp.AttributeTagMapping["printer-name"] = ipp.TagName
	ipp.AttributeTagMapping["printer-info"] = ipp.TagText
	ipp.AttributeTagMapping["printer-make-and-model"] = ipp.TagText
	ipp.AttributeTagMapping["printer-state"] = ipp.TagEnum
	ipp.AttributeTagMapping["printer-state-reasons"] = ipp.TagKeyword
	ipp.AttributeTagMapping["ipp-versions-supported"] = ipp.TagKeyword
	ipp.AttributeTagMapping["operations-supported"] = ipp.TagEnum
	ipp.AttributeTagMapping["multiple-document-jobs-supported"] = ipp.TagBoolean
	ipp.AttributeTagMapping["charset-configured"] = ipp.TagCharset
	ipp.AttributeTagMapping["charset-supported"] = ipp.TagCharset
	ipp.AttributeTagMapping["natural-language-configured"] = ipp.TagLanguage
	ipp.AttributeTagMapping["generated-natural-language-supported"] = ipp.TagLanguage
	ipp.AttributeTagMapping["document-format-default"] = ipp.TagMimeType
	ipp.AttributeTagMapping["document-format-supported"] = ipp.TagMimeType
	ipp.AttributeTagMapping["printer-is-accepting-jobs"] = ipp.TagBoolean
	ipp.AttributeTagMapping["queued-job-count"] = ipp.TagInteger
	ipp.AttributeTagMapping["pdl-override-supported"] = ipp.TagKeyword
	ipp.AttributeTagMapping["printer-up-time"] = ipp.TagInteger
	ipp.AttributeTagMapping["compression-supported"] = ipp.TagKeyword
	ipp.AttributeTagMapping["printer-privacy-policy-uri"] = ipp.TagUri
	ipp.AttributeTagMapping["printer-location"] = ipp.TagText
	ipp.AttributeTagMapping["ppd-name"] = ipp.TagName
	ipp.AttributeTagMapping["job-state-reasons"] = ipp.TagKeyword
	ipp.AttributeTagMapping["job-state"] = ipp.TagEnum
	ipp.AttributeTagMapping["job-uri"] = ipp.TagUri
	ipp.AttributeTagMapping["job-id"] = ipp.TagInteger
	ipp.AttributeTagMapping["job-printer-uri"] = ipp.TagUri
	ipp.AttributeTagMapping["job-name"] = ipp.TagName
	ipp.AttributeTagMapping["job-originating-user-name"] = ipp.TagName
	ipp.AttributeTagMapping["time-at-creation"] = ipp.TagInteger
	ipp.AttributeTagMapping["time-at-completed"] = ipp.TagInteger
	ipp.AttributeTagMapping["job-printer-up-time"] = ipp.TagInteger
}

func ippReadChunkSizeHex(ctx *HandlerContext) string {
	var buf []byte

	for b := make([]byte, 1); ; {
		if n, err := ctx.client.Read(b); err != nil {
			ctx.mod.Error("could not read chunked byte: %v", err)
		} else if n == 0 {
			break
		} else if b[0] == '\n' {
			break
		} else {
			// ctx.mod.Info("buf += 0x%x (%c)", b[0], b[0])
			buf = append(buf, b[0])
		}

		if len(buf) >= IPP_CHUNK_MAX_LINE_SIZE {
			ctx.mod.Warning("buffer size exceeded %d bytes when reading chunk size", IPP_CHUNK_MAX_LINE_SIZE)
			break
		}
	}

	return str.Trim(string(buf))
}

func ippReadChunkSize(ctx *HandlerContext) (uint64, error) {
	if chunkSizeHex := ippReadChunkSizeHex(ctx); chunkSizeHex != "" {
		ctx.mod.Debug("got chunk size: 0x%s", chunkSizeHex)
		return strconv.ParseUint(chunkSizeHex, 16, 64)
	}
	return 0, nil
}

func ippReadChunkedBody(ctx *HandlerContext) ([]byte, error) {
	var chunkedBody []byte
	// read chunked loop
	for {
		// read the next chunk size
		if chunkSize, err := ippReadChunkSize(ctx); err != nil {
			return nil, fmt.Errorf("error reading next chunk size: %v", err)
		} else if chunkSize == 0 {
			break
		} else {
			chunk := make([]byte, chunkSize)
			if n, err := ctx.client.Read(chunk); err != nil {
				return nil, fmt.Errorf("error while reading chunk of %d bytes: %v", chunkSize, err)
			} else if n != int(chunkSize) {
				return nil, fmt.Errorf("expected chunk of size %d, got %d bytes", chunkSize, n)
			} else {
				chunkedBody = append(chunkedBody, chunk...)
			}
		}
	}

	return chunkedBody, nil
}

func ippReadRequestBody(ctx *HandlerContext, http_req *http.Request) (io.ReadCloser, error) {
	ipp_body := http_req.Body

	// check for an Expect 100-continue
	if http_req.Header.Get("Expect") == "100-continue" {
		buf := make([]byte, 4096)

		// inform the client we're ready to read the request body
		ctx.client.Write([]byte("HTTP/1.1 100 Continue\r\n\r\n"))

		if slices.Contains(http_req.TransferEncoding, "chunked") {
			ctx.mod.Debug("detected chunked encoding")
			if body, err := ippReadChunkedBody(ctx); err != nil {
				return nil, err
			} else {
				ipp_body = io.NopCloser(bytes.NewReader(body))
			}
		} else {
			// read the body in a single step
			read, err := ctx.client.Read(buf)
			if err != nil {
				if err == io.EOF {
					return nil, nil
				}
				return nil, fmt.Errorf("error while reading ipp body from %v: %v", ctx.client.RemoteAddr(), err)
			} else if read == 0 {
				return nil, fmt.Errorf("error while reading ipp body from %v: no data", ctx.client.RemoteAddr())
			}

			ipp_body = io.NopCloser(bytes.NewReader(buf[0:read]))
		}
	}

	return ipp_body, nil
}

func ippSendResponse(ctx *HandlerContext, response *ipp.Response) {
	ctx.mod.Debug("SENDING %++v", *response)

	resp_data, err := response.Encode()
	if err != nil {
		ctx.mod.Error("error while encoding ipp response: %v", err)
		return
	}

	headers := [][]byte{
		[]byte("HTTP/1.1 200 OK\r\n"),
		[]byte("Content-Type: application/ipp\r\n"),
		[]byte(fmt.Sprintf("Content-Length: %d\r\n", len(resp_data))),
		[]byte("Connection: close\r\n"),
		[]byte("\r\n"),
	}

	for _, header := range headers {
		if _, err := ctx.client.Write(header); err != nil {
			ctx.mod.Error("error while writing header: %v", err)
			return
		}
	}

	if _, err = ctx.client.Write(resp_data); err != nil {
		ctx.mod.Error("error while writing ipp response data: %v", err)
		return
	}

	ctx.mod.Debug("sent %d of ipp response to %v", len(resp_data), ctx.client.RemoteAddr())
}
```
