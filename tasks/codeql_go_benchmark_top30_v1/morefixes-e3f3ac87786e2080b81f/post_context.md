# Patch-overlapping Go context after the fix

## `prometheus/promhttp/instrument_client.go` (changed lines (52, 53, 54, 55, 63, 64, 65, 66, 67, 68, 74, 84, 85, 86, 87, 99, 100, 101, 102, 103, 104, 111))

```go
// Copyright 2017 The Prometheus Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package promhttp

import (
	"crypto/tls"
	"net/http"
	"net/http/httptrace"
	"time"

	"github.com/prometheus/client_golang/prometheus"
)

// The RoundTripperFunc type is an adapter to allow the use of ordinary
// functions as RoundTrippers. If f is a function with the appropriate
// signature, RountTripperFunc(f) is a RoundTripper that calls f.
type RoundTripperFunc func(req *http.Request) (*http.Response, error)

// RoundTrip implements the RoundTripper interface.
func (rt RoundTripperFunc) RoundTrip(r *http.Request) (*http.Response, error) {
	return rt(r)
}

// InstrumentRoundTripperInFlight is a middleware that wraps the provided
// http.RoundTripper. It sets the provided prometheus.Gauge to the number of
// requests currently handled by the wrapped http.RoundTripper.
//
// See the example for ExampleInstrumentRoundTripperDuration for example usage.
func InstrumentRoundTripperInFlight(gauge prometheus.Gauge, next http.RoundTripper) RoundTripperFunc {
	return RoundTripperFunc(func(r *http.Request) (*http.Response, error) {
		gauge.Inc()
		defer gauge.Dec()
		return next.RoundTrip(r)
	})
}

// InstrumentRoundTripperCounter is a middleware that wraps the provided
// http.RoundTripper to observe the request result with the provided CounterVec.
// The CounterVec must have zero, one, or two non-const non-curried labels. For
// those, the only allowed label names are "code" and "method". The function
// panics otherwise. For the "method" label a predefined default label value set
// is used to filter given values. Values besides predefined values will count
// as `unknown` method.`WithExtraMethods` can be used to add more
// methods to the set. Partitioning of the CounterVec happens by HTTP status code
// and/or HTTP method if the respective instance label names are present in the
// CounterVec. For unpartitioned counting, use a CounterVec with zero labels.
//
// If the wrapped RoundTripper panics or returns a non-nil error, the Counter
// is not incremented.
//
// See the example for ExampleInstrumentRoundTripperDuration for example usage.
func InstrumentRoundTripperCounter(counter *prometheus.CounterVec, next http.RoundTripper, opts ...Option) RoundTripperFunc {
	rtOpts := &option{}
	for _, o := range opts {
		o(rtOpts)
	}

	code, method := checkLabels(counter)

	return RoundTripperFunc(func(r *http.Request) (*http.Response, error) {
		resp, err := next.RoundTrip(r)
		if err == nil {
			counter.With(labels(code, method, r.Method, resp.StatusCode, rtOpts.extraMethods...)).Inc()
		}
		return resp, err
	})
}

// InstrumentRoundTripperDuration is a middleware that wraps the provided
// http.RoundTripper to observe the request duration with the provided
// ObserverVec.  The ObserverVec must have zero, one, or two non-const
// non-curried labels. For those, the only allowed label names are "code" and
// "method". The function panics otherwise. For the "method" label a predefined
// default label value set is used to filter given values. Values besides
// predefined values will count as `unknown` method. `WithExtraMethods`
// can be used to add more methods to the set. The Observe method of the Observer
// in the ObserverVec is called with the request duration in
// seconds. Partitioning happens by HTTP status code and/or HTTP method if the
// respective instance label names are present in the ObserverVec. For
// unpartitioned observations, use an ObserverVec with zero labels. Note that
// partitioning of Histograms is expensive and should be used judiciously.
//
// If the wrapped RoundTripper panics or returns a non-nil error, no values are
// reported.
//
// Note that this method is only guaranteed to never observe negative durations
// if used with Go1.9+.
func InstrumentRoundTripperDuration(obs prometheus.ObserverVec, next http.RoundTripper, opts ...Option) RoundTripperFunc {
	rtOpts := &option{}
	for _, o := range opts {
		o(rtOpts)
	}

	code, method := checkLabels(obs)

	return RoundTripperFunc(func(r *http.Request) (*http.Response, error) {
		start := time.Now()
		resp, err := next.RoundTrip(r)
		if err == nil {
			obs.With(labels(code, method, r.Method, resp.StatusCode, rtOpts.extraMethods...)).Observe(time.Since(start).Seconds())
		}
		return resp, err
	})
}

// InstrumentTrace is used to offer flexibility in instrumenting the available
// httptrace.ClientTrace hook functions. Each function is passed a float64
// representing the time in seconds since the start of the http request. A user
// may choose to use separately buckets Histograms, or implement custom
// instance labels on a per function basis.
type InstrumentTrace struct {
	GotConn              func(float64)
	PutIdleConn          func(float64)
	GotFirstResponseByte func(float64)
	Got100Continue       func(float64)
	DNSStart             func(float64)
	DNSDone              func(float64)
	ConnectStart         func(float64)
	ConnectDone          func(float64)
	TLSHandshakeStart    func(float64)
	TLSHandshakeDone     func(float64)
	WroteHeaders         func(float64)
	Wait100Continue      func(float64)
	WroteRequest         func(float64)
}

// InstrumentRoundTripperTrace is a middleware that wraps the provided
// RoundTripper and reports times to hook functions provided in the
// InstrumentTrace struct. Hook functions that are not present in the provided
// InstrumentTrace struct are ignored. Times reported to the hook functions are
// time since the start of the request. Only with Go1.9+, those times are
// guaranteed to never be negative. (Earlier Go versions are not using a
// monotonic clock.) Note that partitioning of Histograms is expensive and
// should be used judiciously.
//
// For hook functions that receive an error as an argument, no observations are
// made in the event of a non-nil error value.
//
// See the example for ExampleInstrumentRoundTripperDuration for example usage.
func InstrumentRoundTripperTrace(it *InstrumentTrace, next http.RoundTripper) RoundTripperFunc {
	return RoundTripperFunc(func(r *http.Request) (*http.Response, error) {
		start := time.Now()

		trace := &httptrace.ClientTrace{
			GotConn: func(_ httptrace.GotConnInfo) {
				if it.GotConn != nil {
					it.GotConn(time.Since(start).Seconds())
				}
			},
			PutIdleConn: func(err error) {
				if err != nil {
					return
				}
				if it.PutIdleConn != nil {
					it.PutIdleConn(time.Since(start).Seconds())
				}
			},
			DNSStart: func(_ httptrace.DNSStartInfo) {
				if it.DNSStart != nil {
					it.DNSStart(time.Since(start).Seconds())
				}
			},
			DNSDone: func(_ httptrace.DNSDoneInfo) {
				if it.DNSDone != nil {
					it.DNSDone(time.Since(start).Seconds())
				}
			},
			ConnectStart: func(_, _ string) {
				if it.ConnectStart != nil {
					it.ConnectStart(time.Since(start).Seconds())
				}
			},
			ConnectDone: func(_, _ string, err error) {
				if err != nil {
					return
				}
				if it.ConnectDone != nil {
					it.ConnectDone(time.Since(start).Seconds())
				}
			},
			GotFirstResponseByte: func() {
				if it.GotFirstResponseByte != nil {
					it.GotFirstResponseByte(time.Since(start).Seconds())
				}
			},
			Got100Continue: func() {
				if it.Got100Continue != nil {
					it.Got100Continue(time.Since(start).Seconds())
				}
			},
			TLSHandshakeStart: func() {
				if it.TLSHandshakeStart != nil {
					it.TLSHandshakeStart(time.Since(start).Seconds())
				}
			},
			TLSHandshakeDone: func(_ tls.ConnectionState, err error) {
				if err != nil {
					return
				}
				if it.TLSHandshakeDone != nil {
					it.TLSHandshakeDone(time.Since(start).Seconds())
				}
			},
			WroteHeaders: func() {
				if it.WroteHeaders != nil {
					it.WroteHeaders(time.Since(start).Seconds())
				}
			},
			Wait100Continue: func() {
				if it.Wait100Continue != nil {
					it.Wait100Continue(time.Since(start).Seconds())
				}
			},
			WroteRequest: func(_ httptrace.WroteRequestInfo) {
				if it.WroteRequest != nil {
					it.WroteRequest(time.Since(start).Seconds())
				}
			},
		}
		r = r.WithContext(httptrace.WithClientTrace(r.Context(), trace))

		return next.RoundTrip(r)
	})
}
```

## `prometheus/promhttp/instrument_server.go` (changed lines (48, 49, 50, 51, 64, 65, 66, 67, 68, 69, 78, 85, 93, 94, 95, 96, 106, 107, 108, 109, 110, 111, 118, 124, 133, 134, 135, 136, 150, 151, 152, 153, 154, 155, 161, 171, 172, 173, 174, 175, 186, 187, 188, 189, 190, 191, 199, 206, 214, 215, 216, 217, 218, 229, 230, 231, 232, 233, 234, 236, 240, 334, 344, 374, 375, 376, 377, 378, 379, 397, 398, 399, 400, 402, 403, 404, 405, 406, 407, 412, 415, 512, 513, 514, 515))

```go
// Copyright 2017 The Prometheus Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package promhttp

import (
	"errors"
	"net/http"
	"strconv"
	"strings"
	"time"

	dto "github.com/prometheus/client_model/go"

	"github.com/prometheus/client_golang/prometheus"
)

// magicString is used for the hacky label test in checkLabels. Remove once fixed.
const magicString = "zZgWfBxLqvG8kc8IMv3POi2Bb0tZI3vAnBx+gBaFi9FyPzB/CzKUer1yufDa"

// InstrumentHandlerInFlight is a middleware that wraps the provided
// http.Handler. It sets the provided prometheus.Gauge to the number of
// requests currently handled by the wrapped http.Handler.
//
// See the example for InstrumentHandlerDuration for example usage.
func InstrumentHandlerInFlight(g prometheus.Gauge, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		g.Inc()
		defer g.Dec()
		next.ServeHTTP(w, r)
	})
}

// InstrumentHandlerDuration is a middleware that wraps the provided
// http.Handler to observe the request duration with the provided ObserverVec.
// The ObserverVec must have valid metric and label names and must have zero,
// one, or two non-const non-curried labels. For those, the only allowed label
// names are "code" and "method". The function panics otherwise. For the "method"
// label a predefined default label value set is used to filter given values.
// Values besides predefined values will count as `unknown` method.
//`WithExtraMethods` can be used to add more methods to the set. The Observe
// method of the Observer in the ObserverVec is called with the request duration
// in seconds. Partitioning happens by HTTP status code and/or HTTP method if
// the respective instance label names are present in the ObserverVec. For
// unpartitioned observations, use an ObserverVec with zero labels. Note that
// partitioning of Histograms is expensive and should be used judiciously.
//
// If the wrapped Handler does not set a status code, a status code of 200 is assumed.
//
// If the wrapped Handler panics, no values are reported.
//
// Note that this method is only guaranteed to never observe negative durations
// if used with Go1.9+.
func InstrumentHandlerDuration(obs prometheus.ObserverVec, next http.Handler, opts ...Option) http.HandlerFunc {
	mwOpts := &option{}
	for _, o := range opts {
		o(mwOpts)
	}

	code, method := checkLabels(obs)

	if code {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			now := time.Now()
			d := newDelegator(w, nil)
			next.ServeHTTP(d, r)

			obs.With(labels(code, method, r.Method, d.Status(), mwOpts.extraMethods...)).Observe(time.Since(now).Seconds())
		})
	}

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		now := time.Now()
		next.ServeHTTP(w, r)
		obs.With(labels(code, method, r.Method, 0, mwOpts.extraMethods...)).Observe(time.Since(now).Seconds())
	})
}

// InstrumentHandlerCounter is a middleware that wraps the provided http.Handler
// to observe the request result with the provided CounterVec. The CounterVec
// must have valid metric and label names and must have zero, one, or two
// non-const non-curried labels. For those, the only allowed label names are
// "code" and "method". The function panics otherwise. For the "method"
// label a predefined default label value set is used to filter given values.
// Values besides predefined values will count as `unknown` method.
// `WithExtraMethods` can be used to add more methods to the set. Partitioning of the
// CounterVec happens by HTTP status code and/or HTTP method if the respective
// instance label names are present in the CounterVec. For unpartitioned
// counting, use a CounterVec with zero labels.
//
// If the wrapped Handler does not set a status code, a status code of 200 is assumed.
//
// If the wrapped Handler panics, the Counter is not incremented.
//
// See the example for InstrumentHandlerDuration for example usage.
func InstrumentHandlerCounter(counter *prometheus.CounterVec, next http.Handler, opts ...Option) http.HandlerFunc {
	mwOpts := &option{}
	for _, o := range opts {
		o(mwOpts)
	}

	code, method := checkLabels(counter)

	if code {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			d := newDelegator(w, nil)
			next.ServeHTTP(d, r)
			counter.With(labels(code, method, r.Method, d.Status(), mwOpts.extraMethods...)).Inc()
		})
	}

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		next.ServeHTTP(w, r)
		counter.With(labels(code, method, r.Method, 0, mwOpts.extraMethods...)).Inc()
	})
}

// InstrumentHandlerTimeToWriteHeader is a middleware that wraps the provided
// http.Handler to observe with the provided ObserverVec the request duration
// until the response headers are written. The ObserverVec must have valid
// metric and label names and must have zero, one, or two non-const non-curried
// labels. For those, the only allowed label names are "code" and "method". The
// function panics otherwise. For the "method" label a predefined default label
// value set is used to filter given values. Values besides predefined values
// will count as `unknown` method.`WithExtraMethods` can be used to add more
// methods to the set. The Observe method of the Observer in the
// ObserverVec is called with the request duration in seconds. Partitioning
// happens by HTTP status code and/or HTTP method if the respective instance
// label names are present in the ObserverVec. For unpartitioned observations,
// use an ObserverVec with zero labels. Note that partitioning of Histograms is
// expensive and should be used judiciously.
//
// If the wrapped Handler panics before calling WriteHeader, no value is
// reported.
//
// Note that this method is only guaranteed to never observe negative durations
// if used with Go1.9+.
//
// See the example for InstrumentHandlerDuration for example usage.
func InstrumentHandlerTimeToWriteHeader(obs prometheus.ObserverVec, next http.Handler, opts ...Option) http.HandlerFunc {
	mwOpts := &option{}
	for _, o := range opts {
		o(mwOpts)
	}

	code, method := checkLabels(obs)

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		now := time.Now()
		d := newDelegator(w, func(status int) {
			obs.With(labels(code, method, r.Method, status, mwOpts.extraMethods...)).Observe(time.Since(now).Seconds())
		})
		next.ServeHTTP(d, r)
	})
}

// InstrumentHandlerRequestSize is a middleware that wraps the provided
// http.Handler to observe the request size with the provided ObserverVec. The
// ObserverVec must have valid metric and label names and must have zero, one,
// or two non-const non-curried labels. For those, the only allowed label names
// are "code" and "method". The function panics otherwise. For the "method"
// label a predefined default label value set is used to filter given values.
// Values besides predefined values will count as `unknown` method.
// `WithExtraMethods` can be used to add more methods to the set. The Observe
// method of the Observer in the ObserverVec is called with the request size in
// bytes. Partitioning happens by HTTP status code and/or HTTP method if the
// respective instance label names are present in the ObserverVec. For
// unpartitioned observations, use an ObserverVec with zero labels. Note that
// partitioning of Histograms is expensive and should be used judiciously.
//
// If the wrapped Handler does not set a status code, a status code of 200 is assumed.
//
// If the wrapped Handler panics, no values are reported.
//
// See the example for InstrumentHandlerDuration for example usage.
func InstrumentHandlerRequestSize(obs prometheus.ObserverVec, next http.Handler, opts ...Option) http.HandlerFunc {
	mwOpts := &option{}
	for _, o := range opts {
		o(mwOpts)
	}

	code, method := checkLabels(obs)

	if code {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			d := newDelegator(w, nil)
			next.ServeHTTP(d, r)
			size := computeApproximateRequestSize(r)
			obs.With(labels(code, method, r.Method, d.Status(), mwOpts.extraMethods...)).Observe(float64(size))
		})
	}

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		next.ServeHTTP(w, r)
		size := computeApproximateRequestSize(r)
		obs.With(labels(code, method, r.Method, 0, mwOpts.extraMethods...)).Observe(float64(size))
	})
}

// InstrumentHandlerResponseSize is a middleware that wraps the provided
// http.Handler to observe the response size with the provided ObserverVec. The
// ObserverVec must have valid metric and label names and must have zero, one,
// or two non-const non-curried labels. For those, the only allowed label names
// are "code" and "method". The function panics otherwise. For the "method"
// label a predefined default label value set is used to filter given values.
// Values besides predefined values will count as `unknown` method.
// `WithExtraMethods` can be used to add more methods to the set. The Observe
// method of the Observer in the ObserverVec is called with the response size in
// bytes. Partitioning happens by HTTP status code and/or HTTP method if the
// respective instance label names are present in the ObserverVec. For
// unpartitioned observations, use an ObserverVec with zero labels. Note that
// partitioning of Histograms is expensive and should be used judiciously.
//
// If the wrapped Handler does not set a status code, a status code of 200 is assumed.
//
// If the wrapped Handler panics, no values are reported.
//
// See the example for InstrumentHandlerDuration for example usage.
func InstrumentHandlerResponseSize(obs prometheus.ObserverVec, next http.Handler, opts ...Option) http.Handler {
	mwOpts := &option{}
	for _, o := range opts {
		o(mwOpts)
	}

	code, method := checkLabels(obs)

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		d := newDelegator(w, nil)
		next.ServeHTTP(d, r)
		obs.With(labels(code, method, r.Method, d.Status(), mwOpts.extraMethods...)).Observe(float64(d.Written()))
	})
}

// checkLabels returns whether the provided Collector has a non-const,
// non-curried label named "code" and/or "method". It panics if the provided
// Collector does not have a Desc or has more than one Desc or its Desc is
// invalid. It also panics if the Collector has any non-const, non-curried
// labels that are not named "code" or "method".
func checkLabels(c prometheus.Collector) (code bool, method bool) {
	// TODO(beorn7): Remove this hacky way to check for instance labels
	// once Descriptors can have their dimensionality queried.
	var (
		desc *prometheus.Desc
		m    prometheus.Metric
		pm   dto.Metric
		lvs  []string
	)

	// Get the Desc from the Collector.
	descc := make(chan *prometheus.Desc, 1)
	c.Describe(descc)

	select {
	case desc = <-descc:
	default:
		panic("no description provided by collector")
	}
	select {
	case <-descc:
		panic("more than one description provided by collector")
	default:
	}

	close(descc)

	// Make sure the Collector has a valid Desc by registering it with a
	// temporary registry.
	prometheus.NewRegistry().MustRegister(c)

	// Create a ConstMetric with the Desc. Since we don't know how many
	// variable labels there are, try for as long as it needs.
	for err := errors.New("dummy"); err != nil; lvs = append(lvs, magicString) {
		m, err = prometheus.NewConstMetric(desc, prometheus.UntypedValue, 0, lvs...)
	}

	// Write out the metric into a proto message and look at the labels.
	// If the value is not the magicString, it is a constLabel, which doesn't interest us.
	// If the label is curried, it doesn't interest us.
	// In all other cases, only "code" or "method" is allowed.
	if err := m.Write(&pm); err != nil {
		panic("error checking metric for labels")
	}
	for _, label := range pm.Label {
		name, value := label.GetName(), label.GetValue()
		if value != magicString || isLabelCurried(c, name) {
			continue
		}
		switch name {
		case "code":
			code = true
		case "method":
			method = true
		default:
			panic("metric partitioned with non-supported labels")
		}
	}
	return
}

func isLabelCurried(c prometheus.Collector, label string) bool {
	// This is even hackier than the label test above.
	// We essentially try to curry again and see if it works.
	// But for that, we need to type-convert to the two
	// types we use here, ObserverVec or *CounterVec.
	switch v := c.(type) {
	case *prometheus.CounterVec:
		if _, err := v.CurryWith(prometheus.Labels{label: "dummy"}); err == nil {
			return false
		}
	case prometheus.ObserverVec:
		if _, err := v.CurryWith(prometheus.Labels{label: "dummy"}); err == nil {
			return false
		}
	default:
		panic("unsupported metric vec type")
	}
	return true
}

// emptyLabels is a one-time allocation for non-partitioned metrics to avoid
// unnecessary allocations on each request.
var emptyLabels = prometheus.Labels{}

func labels(code, method bool, reqMethod string, status int, extraMethods ...string) prometheus.Labels {
	if !(code || method) {
		return emptyLabels
	}
	labels := prometheus.Labels{}

	if code {
		labels["code"] = sanitizeCode(status)
	}
	if method {
		labels["method"] = sanitizeMethod(reqMethod, extraMethods...)
	}

	return labels
}

func computeApproximateRequestSize(r *http.Request) int {
	s := 0
	if r.URL != nil {
		s += len(r.URL.String())
	}

	s += len(r.Method)
	s += len(r.Proto)
	for name, values := range r.Header {
		s += len(name)
		for _, value := range values {
			s += len(value)
		}
	}
	s += len(r.Host)

	// N.B. r.Form and r.MultipartForm are assumed to be included in r.URL.

	if r.ContentLength != -1 {
		s += int(r.ContentLength)
	}
	return s
}

// If the wrapped http.Handler has a known method, it will be sanitized and returned.
// Otherwise, "unknown" will be returned. The known method list can be extended
// as needed by using extraMethods parameter.
func sanitizeMethod(m string, extraMethods ...string) string {
	// See https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods for
	// the methods chosen as default.
	switch m {
	case "GET", "get":
		return "get"
	case "PUT", "put":
		return "put"
	case "HEAD", "head":
		return "head"
	case "POST", "post":
		return "post"
	case "DELETE", "delete":
		return "delete"
	case "CONNECT", "connect":
		return "connect"
	case "OPTIONS", "options":
		return "options"
	case "NOTIFY", "notify":
		return "notify"
	case "TRACE", "trace":
		return "trace"
	case "PATCH", "patch":
		return "patch"
	default:
		for _, method := range extraMethods {
			if strings.EqualFold(m, method) {
				return strings.ToLower(m)
			}
		}
		return "unknown"
	}
}

// If the wrapped http.Handler has not set a status code, i.e. the value is
// currently 0, sanitizeCode will return 200, for consistency with behavior in
// the stdlib.
func sanitizeCode(s int) string {
	// See for accepted codes https://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml
	switch s {
	case 100:
		return "100"
	case 101:
		return "101"

	case 200, 0:
		return "200"
	case 201:
		return "201"
	case 202:
		return "202"
	case 203:
		return "203"
	case 204:
		return "204"
	case 205:
		return "205"
	case 206:
		return "206"

	case 300:
		return "300"
	case 301:
		return "301"
	case 302:
		return "302"
	case 304:
		return "304"
	case 305:
		return "305"
	case 307:
		return "307"

	case 400:
		return "400"
	case 401:
		return "401"
	case 402:
		return "402"
	case 403:
		return "403"
	case 404:
		return "404"
	case 405:
		return "405"
	case 406:
		return "406"
	case 407:
		return "407"
	case 408:
		return "408"
	case 409:
		return "409"
	case 410:
		return "410"
	case 411:
		return "411"
	case 412:
		return "412"
	case 413:
		return "413"
	case 414:
		return "414"
	case 415:
		return "415"
	case 416:
		return "416"
	case 417:
		return "417"
	case 418:
		return "418"

	case 500:
		return "500"
	case 501:
		return "501"
	case 502:
		return "502"
	case 503:
		return "503"
	case 504:
		return "504"
	case 505:
		return "505"

	case 428:
		return "428"
	case 429:
		return "429"
	case 431:
		return "431"
	case 511:
		return "511"

	default:
		if s >= 100 && s <= 599 {
			return strconv.Itoa(s)
		}
		return "unknown"
	}
}
```

## `prometheus/promhttp/instrument_server_test.go` (changed lines (207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322))

```go
// Copyright 2017 The Prometheus Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package promhttp

import (
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/prometheus/client_golang/prometheus"
)

func TestLabelCheck(t *testing.T) {
	scenarios := map[string]struct {
		metricName    string // Defaults to "c".
		varLabels     []string
		constLabels   []string
		curriedLabels []string
		ok            bool
	}{
		"empty": {
			varLabels:     []string{},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            true,
		},
		"code as single var label": {
			varLabels:     []string{"code"},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            true,
		},
		"method as single var label": {
			varLabels:     []string{"method"},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            true,
		},
		"code and method as var labels": {
			varLabels:     []string{"method", "code"},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            true,
		},
		"valid case with all labels used": {
			varLabels:     []string{"code", "method"},
			constLabels:   []string{"foo", "bar"},
			curriedLabels: []string{"dings", "bums"},
			ok:            true,
		},
		"all labels used with an invalid const label name": {
			varLabels:     []string{"code", "method"},
			constLabels:   []string{"in-valid", "bar"},
			curriedLabels: []string{"dings", "bums"},
			ok:            false,
		},
		"unsupported var label": {
			varLabels:     []string{"foo"},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            false,
		},
		"mixed var labels": {
			varLabels:     []string{"method", "foo", "code"},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            false,
		},
		"unsupported var label but curried": {
			varLabels:     []string{},
			constLabels:   []string{},
			curriedLabels: []string{"foo"},
			ok:            true,
		},
		"mixed var labels but unsupported curried": {
			varLabels:     []string{"code", "method"},
			constLabels:   []string{},
			curriedLabels: []string{"foo"},
			ok:            true,
		},
		"supported label as const and curry": {
			varLabels:     []string{},
			constLabels:   []string{"code"},
			curriedLabels: []string{"method"},
			ok:            true,
		},
		"supported label as const and curry with unsupported as var": {
			varLabels:     []string{"foo"},
			constLabels:   []string{"code"},
			curriedLabels: []string{"method"},
			ok:            false,
		},
		"invalid name and otherwise empty": {
			metricName:    "in-valid",
			varLabels:     []string{},
			constLabels:   []string{},
			curriedLabels: []string{},
			ok:            false,
		},
		"invalid name with all the otherwise valid labels": {
			metricName:    "in-valid",
			varLabels:     []string{"code", "method"},
			constLabels:   []string{"foo", "bar"},
			curriedLabels: []string{"dings", "bums"},
			ok:            false,
		},
	}

	for name, sc := range scenarios {
		t.Run(name, func(t *testing.T) {
			metricName := sc.metricName
			if metricName == "" {
				metricName = "c"
			}
			constLabels := prometheus.Labels{}
			for _, l := range sc.constLabels {
				constLabels[l] = "dummy"
			}
			c := prometheus.NewCounterVec(
				prometheus.CounterOpts{
					Name:        metricName,
					Help:        "c help",
					ConstLabels: constLabels,
				},
				append(sc.varLabels, sc.curriedLabels...),
			)
			o := prometheus.ObserverVec(prometheus.NewHistogramVec(
				prometheus.HistogramOpts{
					Name:        metricName,
					Help:        "c help",
					ConstLabels: constLabels,
				},
				append(sc.varLabels, sc.curriedLabels...),
			))
			for _, l := range sc.curriedLabels {
				c = c.MustCurryWith(prometheus.Labels{l: "dummy"})
				o = o.MustCurryWith(prometheus.Labels{l: "dummy"})
			}

			func() {
				defer func() {
					if err := recover(); err != nil {
						if sc.ok {
							t.Error("unexpected panic:", err)
						}
					} else if !sc.ok {
						t.Error("expected panic")
					}
				}()
				InstrumentHandlerCounter(c, nil)
			}()
			func() {
				defer func() {
					if err := recover(); err != nil {
						if sc.ok {
							t.Error("unexpected panic:", err)
						}
					} else if !sc.ok {
						t.Error("expected panic")
					}
				}()
				InstrumentHandlerDuration(o, nil)
			}()
			if sc.ok {
				// Test if wantCode and wantMethod were detected correctly.
				var wantCode, wantMethod bool
				for _, l := range sc.varLabels {
					if l == "code" {
						wantCode = true
					}
					if l == "method" {
						wantMethod = true
					}
				}
				gotCode, gotMethod := checkLabels(c)
				if gotCode != wantCode {
					t.Errorf("wanted code=%t for counter, got code=%t", wantCode, gotCode)
				}
				if gotMethod != wantMethod {
					t.Errorf("wanted method=%t for counter, got method=%t", wantMethod, gotMethod)
				}
				gotCode, gotMethod = checkLabels(o)
				if gotCode != wantCode {
					t.Errorf("wanted code=%t for observer, got code=%t", wantCode, gotCode)
				}
				if gotMethod != wantMethod {
					t.Errorf("wanted method=%t for observer, got method=%t", wantMethod, gotMethod)
				}
			}
		})
	}
}

func TestLabels(t *testing.T) {
	scenarios := map[string]struct {
		varLabels    []string
		reqMethod    string
		respStatus   int
		extraMethods []string
		wantLabels   prometheus.Labels
		ok           bool
	}{
		"empty": {
			varLabels:  []string{},
			wantLabels: emptyLabels,
			reqMethod:  "GET",
			respStatus: 200,
			ok:         true,
		},
		"code as single var label": {
			varLabels:  []string{"code"},
			reqMethod:  "GET",
			respStatus: 200,
			wantLabels: prometheus.Labels{"code": "200"},
			ok:         true,
		},
		"code as single var label and out-of-range code": {
			varLabels:  []string{"code"},
			reqMethod:  "GET",
			respStatus: 99,
			wantLabels: prometheus.Labels{"code": "unknown"},
			ok:         true,
		},
		"code as single var label and in-range but unrecognized code": {
			varLabels:  []string{"code"},
			reqMethod:  "GET",
			respStatus: 308,
			wantLabels: prometheus.Labels{"code": "308"},
			ok:         true,
		},
		"method as single var label": {
			varLabels:  []string{"method"},
			reqMethod:  "GET",
			respStatus: 200,
			wantLabels: prometheus.Labels{"method": "get"},
			ok:         true,
		},
		"method as single var label and unknown method": {
			varLabels:  []string{"method"},
			reqMethod:  "CUSTOM_METHOD",
			respStatus: 200,
			wantLabels: prometheus.Labels{"method": "unknown"},
			ok:         true,
		},
		"code and method as var labels": {
			varLabels:  []string{"method", "code"},
			reqMethod:  "GET",
			respStatus: 200,
			wantLabels: prometheus.Labels{"method": "get", "code": "200"},
			ok:         true,
		},
		"method as single var label with extra methods specified": {
			varLabels:    []string{"method"},
			reqMethod:    "CUSTOM_METHOD",
			respStatus:   200,
			extraMethods: []string{"CUSTOM_METHOD", "CUSTOM_METHOD_1"},
			wantLabels:   prometheus.Labels{"method": "custom_method"},
			ok:           true,
		},
		"all labels used with an unknown method and out-of-range code": {
			varLabels:  []string{"code", "method"},
			reqMethod:  "CUSTOM_METHOD",
			respStatus: 99,
			wantLabels: prometheus.Labels{"method": "unknown", "code": "unknown"},
			ok:         false,
		},
	}
	checkLabels := func(labels []string) (gotCode bool, gotMethod bool) {
		for _, label := range labels {
			switch label {
			case "code":
				gotCode = true
			case "method":
				gotMethod = true
			default:
				panic("metric partitioned with non-supported labels for this test")
			}
		}
		return
	}
	equalLabels := func(gotLabels, wantLabels prometheus.Labels) bool {
		if len(gotLabels) != len(wantLabels) {
			return false
		}
		for ln, lv := range gotLabels {
			olv, ok := wantLabels[ln]
			if !ok {
				return false
			}
			if olv != lv {
				return false
			}
		}
		return true
	}

	for name, sc := range scenarios {
		t.Run(name, func(t *testing.T) {
			if sc.ok {
				gotCode, gotMethod := checkLabels(sc.varLabels)
				gotLabels := labels(gotCode, gotMethod, sc.reqMethod, sc.respStatus, sc.extraMethods...)
				if !equalLabels(gotLabels, sc.wantLabels) {
					t.Errorf("wanted labels=%v for counter, got code=%v", sc.wantLabels, gotLabels)
				}
			}
		})
	}
}

func TestMiddlewareAPI(t *testing.T) {
	reg := prometheus.NewRegistry()

	inFlightGauge := prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "in_flight_requests",
		Help: "A gauge of requests currently being served by the wrapped handler.",
	})

	counter := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Name: "api_requests_total",
			Help: "A counter for requests to the wrapped handler.",
		},
		[]string{"code", "method"},
	)

	histVec := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:        "response_duration_seconds",
			Help:        "A histogram of request latencies.",
			Buckets:     prometheus.DefBuckets,
			ConstLabels: prometheus.Labels{"handler": "api"},
		},
		[]string{"method"},
	)

	writeHeaderVec := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:        "write_header_duration_seconds",
			Help:        "A histogram of time to first write latencies.",
			Buckets:     prometheus.DefBuckets,
			ConstLabels: prometheus.Labels{"handler": "api"},
		},
		[]string{},
	)

	responseSize := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:    "push_request_size_bytes",
			Help:    "A histogram of request sizes for requests.",
			Buckets: []float64{200, 500, 900, 1500},
		},
		[]string{},
	)

	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("OK"))
	})

	reg.MustRegister(inFlightGauge, counter, histVec, responseSize, writeHeaderVec)

	chain := InstrumentHandlerInFlight(inFlightGauge,
		InstrumentHandlerCounter(counter,
			InstrumentHandlerDuration(histVec,
				InstrumentHandlerTimeToWriteHeader(writeHeaderVec,
					InstrumentHandlerResponseSize(responseSize, handler),
				),
			),
		),
	)

	r, _ := http.NewRequest("GET", "www.example.com", nil)
	w := httptest.NewRecorder()
	chain.ServeHTTP(w, r)
}

func TestInstrumentTimeToFirstWrite(t *testing.T) {
	var i int
	dobs := &responseWriterDelegator{
		ResponseWriter: httptest.NewRecorder(),
		observeWriteHeader: func(status int) {
			i = status
		},
	}
	d := newDelegator(dobs, nil)

	d.WriteHeader(http.StatusOK)

	if i != http.StatusOK {
		t.Fatalf("failed to execute observeWriteHeader")
	}
}

// testResponseWriter is an http.ResponseWriter that also implements
// http.CloseNotifier, http.Flusher, and io.ReaderFrom.
type testResponseWriter struct {
	closeNotifyCalled, flushCalled, readFromCalled bool
}

func (t *testResponseWriter) Header() http.Header       { return nil }
func (t *testResponseWriter) Write([]byte) (int, error) { return 0, nil }
func (t *testResponseWriter) WriteHeader(int)           {}
func (t *testResponseWriter) CloseNotify() <-chan bool {
	t.closeNotifyCalled = true
	return nil
}
func (t *testResponseWriter) Flush() { t.flushCalled = true }
func (t *testResponseWriter) ReadFrom(io.Reader) (int64, error) {
	t.readFromCalled = true
	return 0, nil
}

// testFlusher is an http.ResponseWriter that also implements http.Flusher.
type testFlusher struct {
	flushCalled bool
}

func (t *testFlusher) Header() http.Header       { return nil }
func (t *testFlusher) Write([]byte) (int, error) { return 0, nil }
func (t *testFlusher) WriteHeader(int)           {}
func (t *testFlusher) Flush()                    { t.flushCalled = true }

func TestInterfaceUpgrade(t *testing.T) {
	w := &testResponseWriter{}
	d := newDelegator(w, nil)
	//nolint:staticcheck // Ignore SA1019. http.CloseNotifier is deprecated but we keep it here to not break existing users.
	d.(http.CloseNotifier).CloseNotify()
	if !w.closeNotifyCalled {
		t.Error("CloseNotify not called")
	}
	d.(http.Flusher).Flush()
	if !w.flushCalled {
		t.Error("Flush not called")
	}
	d.(io.ReaderFrom).ReadFrom(nil)
	if !w.readFromCalled {
		t.Error("ReadFrom not called")
	}
	if _, ok := d.(http.Hijacker); ok {
		t.Error("delegator unexpectedly implements http.Hijacker")
	}

	f := &testFlusher{}
	d = newDelegator(f, nil)
	//nolint:staticcheck // Ignore SA1019. http.CloseNotifier is deprecated but we keep it here to not break existing users.
	if _, ok := d.(http.CloseNotifier); ok {
		t.Error("delegator unexpectedly implements http.CloseNotifier")
	}
	d.(http.Flusher).Flush()
	if !w.flushCalled {
		t.Error("Flush not called")
	}
	if _, ok := d.(io.ReaderFrom); ok {
		t.Error("delegator unexpectedly implements io.ReaderFrom")
	}
	if _, ok := d.(http.Hijacker); ok {
		t.Error("delegator unexpectedly implements http.Hijacker")
	}
}

func ExampleInstrumentHandlerDuration() {
	inFlightGauge := prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "in_flight_requests",
		Help: "A gauge of requests currently being served by the wrapped handler.",
	})

	counter := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Name: "api_requests_total",
			Help: "A counter for requests to the wrapped handler.",
		},
		[]string{"code", "method"},
	)

	// duration is partitioned by the HTTP method and handler. It uses custom
	// buckets based on the expected request duration.
	duration := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:    "request_duration_seconds",
			Help:    "A histogram of latencies for requests.",
			Buckets: []float64{.25, .5, 1, 2.5, 5, 10},
		},
		[]string{"handler", "method"},
	)

	// responseSize has no labels, making it a zero-dimensional
	// ObserverVec.
	responseSize := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:    "response_size_bytes",
			Help:    "A histogram of response sizes for requests.",
			Buckets: []float64{200, 500, 900, 1500},
		},
		[]string{},
	)

	// Create the handlers that will be wrapped by the middleware.
	pushHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("Push"))
	})
	pullHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("Pull"))
	})

	// Register all of the metrics in the standard registry.
	prometheus.MustRegister(inFlightGauge, counter, duration, responseSize)

	// Instrument the handlers with all the metrics, injecting the "handler"
	// label by currying.
	pushChain := InstrumentHandlerInFlight(inFlightGauge,
		InstrumentHandlerDuration(duration.MustCurryWith(prometheus.Labels{"handler": "push"}),
			InstrumentHandlerCounter(counter,
				InstrumentHandlerResponseSize(responseSize, pushHandler),
			),
		),
	)
	pullChain := InstrumentHandlerInFlight(inFlightGauge,
		InstrumentHandlerDuration(duration.MustCurryWith(prometheus.Labels{"handler": "pull"}),
			InstrumentHandlerCounter(counter,
				InstrumentHandlerResponseSize(responseSize, pullHandler),
			),
		),
	)

	http.Handle("/metrics", Handler())
	http.Handle("/push", pushChain)
	http.Handle("/pull", pullChain)

	if err := http.ListenAndServe(":3000", nil); err != nil {
		log.Fatal(err)
	}
}
```

## `prometheus/promhttp/option.go` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31))

```go
// Copyright 2022 The Prometheus Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package promhttp

// Option are used to configure a middleware or round tripper..
type Option func(*option)

type option struct {
	extraMethods []string
}

// WithExtraMethods adds additional HTTP methods to the list of allowed methods.
// See https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods for the default list.
//
// See the example for ExampleInstrumentHandlerWithExtraMethods for example usage.
func WithExtraMethods(methods ...string) Option {
	return func(o *option) {
		o.extraMethods = methods
	}
}
```

## `prometheus/promhttp/option_test.go` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65))

```go
// Copyright 2022 The Prometheus Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package promhttp

import (
	"log"
	"net/http"

	"github.com/prometheus/client_golang/prometheus"
)

func ExampleInstrumentHandlerWithExtraMethods() {
	counter := prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Name: "api_requests_total",
			Help: "A counter for requests to the wrapped handler.",
		},
		[]string{"code", "method"},
	)

	// duration is partitioned by the HTTP method and handler. It uses custom
	// buckets based on the expected request duration.
	duration := prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Name:    "request_duration_seconds",
			Help:    "A histogram of latencies for requests.",
			Buckets: []float64{.25, .5, 1, 2.5, 5, 10},
		},
		[]string{"handler", "method"},
	)

	// Create the handlers that will be wrapped by the middleware.
	pullHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("Pull"))
	})

	// Specify additional HTTP methods to be added to the label allow list.
	opts := WithExtraMethods("CUSTOM_METHOD")

	// Instrument the handlers with all the metrics, injecting the "handler"
	// label by currying.
	pullChain :=
		InstrumentHandlerDuration(duration.MustCurryWith(prometheus.Labels{"handler": "pull"}),
			InstrumentHandlerCounter(counter, pullHandler, opts),
			opts,
		)

	http.Handle("/metrics", Handler())
	http.Handle("/pull", pullChain)

	if err := http.ListenAndServe(":3000", nil); err != nil {
		log.Fatal(err)
	}
}
```
