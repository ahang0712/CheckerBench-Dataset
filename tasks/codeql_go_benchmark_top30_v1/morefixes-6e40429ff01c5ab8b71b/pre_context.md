# Patch-overlapping Go context before the fix

## `binder.go` (changed lines (216, 232))

```go
// Copyright (c) 2012-2016 The Revel Framework Authors, All rights reserved.
// Revel Framework source code and usage is governed by a MIT style
// license that can be found in the LICENSE file.

package revel

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"mime/multipart"
	"os"
	"reflect"
	"strconv"
	"strings"
	"time"
)

// A Binder translates between string parameters and Go data structures.
type Binder struct {
	// Bind takes the name and type of the desired parameter and constructs it
	// from one or more values from Params.
	//
	// Example
	//
	// Request:
	//   url?id=123&ol[0]=1&ol[1]=2&ul[]=str&ul[]=array&user.Name=rob
	//
	// Action:
	//   Example.Action(id int, ol []int, ul []string, user User)
	//
	// Calls:
	//   Bind(params, "id", int): 123
	//   Bind(params, "ol", []int): {1, 2}
	//   Bind(params, "ul", []string): {"str", "array"}
	//   Bind(params, "user", User): User{Name:"rob"}
	//
	// Note that only exported struct fields may be bound.
	Bind func(params *Params, name string, typ reflect.Type) reflect.Value

	// Unbind serializes a given value to one or more URL parameters of the given
	// name.
	Unbind func(output map[string]string, name string, val interface{})
}

var binderLog = RevelLog.New("section", "binder")

// ValueBinder is adapter for easily making one-key-value binders.
func ValueBinder(f func(value string, typ reflect.Type) reflect.Value) func(*Params, string, reflect.Type) reflect.Value {
	return func(params *Params, name string, typ reflect.Type) reflect.Value {
		vals, ok := params.Values[name]
		if !ok || len(vals) == 0 {
			return reflect.Zero(typ)
		}
		return f(vals[0], typ)
	}
}

// Revel's default date and time constants
const (
	DefaultDateFormat     = "2006-01-02"
	DefaultDateTimeFormat = "2006-01-02 15:04"
)

// Binders type and kind definition
var (
	// These are the lookups to find a Binder for any type of data.
	// The most specific binder found will be used (Type before Kind)
	TypeBinders = make(map[reflect.Type]Binder)
	KindBinders = make(map[reflect.Kind]Binder)

	// Applications can add custom time formats to this array, and they will be
	// automatically attempted when binding a time.Time.
	TimeFormats = []string{}

	DateFormat     string
	DateTimeFormat string
	TimeZone       = time.UTC

	IntBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			if len(val) == 0 {
				return reflect.Zero(typ)
			}
			intValue, err := strconv.ParseInt(val, 10, 64)
			if err != nil {
				binderLog.Warn("IntBinder Conversion Error", "error", err)
				return reflect.Zero(typ)
			}
			pValue := reflect.New(typ)
			pValue.Elem().SetInt(intValue)
			return pValue.Elem()
		}),
		Unbind: func(output map[string]string, key string, val interface{}) {
			output[key] = fmt.Sprintf("%d", val)
		},
	}

	UintBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			if len(val) == 0 {
				return reflect.Zero(typ)
			}
			uintValue, err := strconv.ParseUint(val, 10, 64)
			if err != nil {
				binderLog.Warn("UintBinder Conversion Error", "error", err)
				return reflect.Zero(typ)
			}
			pValue := reflect.New(typ)
			pValue.Elem().SetUint(uintValue)
			return pValue.Elem()
		}),
		Unbind: func(output map[string]string, key string, val interface{}) {
			output[key] = fmt.Sprintf("%d", val)
		},
	}

	FloatBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			if len(val) == 0 {
				return reflect.Zero(typ)
			}
			floatValue, err := strconv.ParseFloat(val, 64)
			if err != nil {
				binderLog.Warn("FloatBinder Conversion Error", "error", err)
				return reflect.Zero(typ)
			}
			pValue := reflect.New(typ)
			pValue.Elem().SetFloat(floatValue)
			return pValue.Elem()
		}),
		Unbind: func(output map[string]string, key string, val interface{}) {
			output[key] = fmt.Sprintf("%f", val)
		},
	}

	StringBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			return reflect.ValueOf(val)
		}),
		Unbind: func(output map[string]string, name string, val interface{}) {
			output[name] = val.(string)
		},
	}

	// Booleans support a various value formats,
	// refer `revel.Atob` method.
	BoolBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			return reflect.ValueOf(Atob(val))
		}),
		Unbind: func(output map[string]string, name string, val interface{}) {
			output[name] = fmt.Sprintf("%t", val)
		},
	}

	PointerBinder = Binder{
		Bind: func(params *Params, name string, typ reflect.Type) reflect.Value {
			v := Bind(params, name, typ.Elem())
			if v.CanAddr() {
				return v.Addr()
			}

			return v
		},
		Unbind: func(output map[string]string, name string, val interface{}) {
			Unbind(output, name, reflect.ValueOf(val).Elem().Interface())
		},
	}

	TimeBinder = Binder{
		Bind: ValueBinder(func(val string, typ reflect.Type) reflect.Value {
			for _, f := range TimeFormats {
				if r, err := time.ParseInLocation(f, val, TimeZone); err == nil {
					return reflect.ValueOf(r)
				}
			}
			return reflect.Zero(typ)
		}),
		Unbind: func(output map[string]string, name string, val interface{}) {
			var (
				t       = val.(time.Time)
				format  = DateTimeFormat
				h, m, s = t.Clock()
			)
			if h == 0 && m == 0 && s == 0 {
				format = DateFormat
			}
			output[name] = t.Format(format)
		},
	}

	MapBinder = Binder{
		Bind:   bindMap,
		Unbind: unbindMap,
	}
)

// Used to keep track of the index for individual keyvalues.
type sliceValue struct {
	index int           // Index extracted from brackets.  If -1, no index was provided.
	value reflect.Value // the bound value for this slice element.
}

// This function creates a slice of the given type, Binds each of the individual
// elements, and then sets them to their appropriate location in the slice.
// If elements are provided without an explicit index, they are added (in
// unspecified order) to the end of the slice.
func bindSlice(params *Params, name string, typ reflect.Type) reflect.Value {
	// Collect an array of slice elements with their indexes (and the max index).
	maxIndex := -1
	numNoIndex := 0
	sliceValues := []sliceValue{}

	// Factor out the common slice logic (between form values and files).
	processElement := func(key string, vals []string, files []*multipart.FileHeader) {
		if !strings.HasPrefix(key, name+"[") {
			return
		}

		// Extract the index, and the index where a sub-key starts. (e.g. field[0].subkey)
		index := -1
		leftBracket, rightBracket := len(name), strings.Index(key[len(name):], "]")+len(name)
		if rightBracket > leftBracket+1 {
			index, _ = strconv.Atoi(key[leftBracket+1 : rightBracket])
		}
		subKeyIndex := rightBracket + 1

		// Handle the indexed case.
		if index > -1 {
			if index > maxIndex {
				maxIndex = index
			}
			sliceValues = append(sliceValues, sliceValue{
				index: index,
				value: Bind(params, key[:subKeyIndex], typ.Elem()),
			})
			return
		}

		// It's an un-indexed element.  (e.g. element[])
		numNoIndex += len(vals) + len(files)
		for _, val := range vals {
			// Unindexed values can only be direct-bound.
			sliceValues = append(sliceValues, sliceValue{
				index: -1,
				value: BindValue(val, typ.Elem()),
			})
		}

		for _, fileHeader := range files {
			sliceValues = append(sliceValues, sliceValue{
				index: -1,
				value: BindFile(fileHeader, typ.Elem()),
			})
		}
	}

	for key, vals := range params.Values {
		processElement(key, vals, nil)
	}
	for key, fileHeaders := range params.Files {
		processElement(key, nil, fileHeaders)
	}

	resultArray := reflect.MakeSlice(typ, maxIndex+1, maxIndex+1+numNoIndex)
	for _, sv := range sliceValues {
		if sv.index != -1 {
			resultArray.Index(sv.index).Set(sv.value)
		} else {
			resultArray = reflect.Append(resultArray, sv.value)
		}
	}

	return resultArray
}

// Break on dots and brackets.
// e.g. bar => "bar", bar.baz => "bar", bar[0] => "bar"
func nextKey(key string) string {
	fieldLen := strings.IndexAny(key, ".[")
	if fieldLen == -1 {
		return key
	}
	return key[:fieldLen]
}

func unbindSlice(output map[string]string, name string, val interface{}) {
	v := reflect.ValueOf(val)
	for i := 0; i < v.Len(); i++ {
		Unbind(output, fmt.Sprintf("%s[%d]", name, i), v.Index(i).Interface())
	}
}

func bindStruct(params *Params, name string, typ reflect.Type) reflect.Value {
	resultPointer := reflect.New(typ)
	result := resultPointer.Elem()
	if params.JSON != nil {
		// Try to inject the response as a json into the created result
		if err := json.Unmarshal(params.JSON, resultPointer.Interface()); err != nil {
			binderLog.Error("bindStruct Unable to unmarshal request", "name", name, "error", err, "data", string(params.JSON))
		}
		return result
	}
	fieldValues := make(map[string]reflect.Value)
	for key := range params.Values {
		if !strings.HasPrefix(key, name+".") {
			continue
		}

		// Get the name of the struct property.
		// Strip off the prefix. e.g. foo.bar.baz => bar.baz
		suffix := key[len(name)+1:]
		fieldName := nextKey(suffix)
		fieldLen := len(fieldName)

		if _, ok := fieldValues[fieldName]; !ok {
			// Time to bind this field.  Get it and make sure we can set it.
			fieldValue := result.FieldByName(fieldName)
			if !fieldValue.IsValid() {
				binderLog.Warn("bindStruct Field not found", "name", fieldName)
				continue
			}
			if !fieldValue.CanSet() {
				binderLog.Warn("bindStruct Field not settable", "name", fieldName)
				continue
			}
			boundVal := Bind(params, key[:len(name)+1+fieldLen], fieldValue.Type())
			fieldValue.Set(boundVal)
			fieldValues[fieldName] = boundVal
		}
	}

	return result
}

func unbindStruct(output map[string]string, name string, iface interface{}) {
	val := reflect.ValueOf(iface)
	typ := val.Type()
	for i := 0; i < val.NumField(); i++ {
		structField := typ.Field(i)
		fieldValue := val.Field(i)

		// PkgPath is specified to be empty exactly for exported fields.
		if structField.PkgPath == "" {
			Unbind(output, fmt.Sprintf("%s.%s", name, structField.Name), fieldValue.Interface())
		}
	}
}

// Helper that returns an upload of the given name, or nil.
func getMultipartFile(params *Params, name string) multipart.File {
	for _, fileHeader := range params.Files[name] {
		file, err := fileHeader.Open()
		if err == nil {
			return file
		}
		binderLog.Warn("getMultipartFile: Failed to open uploaded file", "name", name, "error", err)
	}
	return nil
}

func bindFile(params *Params, name string, typ reflect.Type) reflect.Value {
	reader := getMultipartFile(params, name)
	if reader == nil {
		return reflect.Zero(typ)
	}

	// If it's already stored in a temp file, just return that.
	if osFile, ok := reader.(*os.File); ok {
		return reflect.ValueOf(osFile)
	}

	// Otherwise, have to store it.
	tmpFile, err := ioutil.TempFile("", "revel-upload")
	if err != nil {
		binderLog.Warn("bindFile: Failed to create a temp file to store upload", "name", name, "error", err)
		return reflect.Zero(typ)
	}

	// Register it to be deleted after the request is done.
	params.tmpFiles = append(params.tmpFiles, tmpFile)

	_, err = io.Copy(tmpFile, reader)
	if err != nil {
		binderLog.Warn("bindFile: Failed to copy upload to temp file", "name", name, "error", err)
		return reflect.Zero(typ)
	}

	_, err = tmpFile.Seek(0, 0)
	if err != nil {
		binderLog.Warn("bindFile: Failed to seek to beginning of temp file", "name", name, "error", err)
		return reflect.Zero(typ)
	}

	return reflect.ValueOf(tmpFile)
}

func bindByteArray(params *Params, name string, typ reflect.Type) reflect.Value {
	if reader := getMultipartFile(params, name); reader != nil {
		b, err := ioutil.ReadAll(reader)
		if err == nil {
			return reflect.ValueOf(b)
		}
		binderLog.Warn("bindByteArray: Error reading uploaded file contents", "name", name, "error", err)
	}
	return reflect.Zero(typ)
}

func bindReadSeeker(params *Params, name string, typ reflect.Type) reflect.Value {
	if reader := getMultipartFile(params, name); reader != nil {
		return reflect.ValueOf(reader.(io.ReadSeeker))
	}
	return reflect.Zero(typ)
}

// bindMap converts parameters using map syntax into the corresponding map. e.g.:
//   params["a[5]"]=foo, name="a", typ=map[int]string => map[int]string{5: "foo"}
func bindMap(params *Params, name string, typ reflect.Type) reflect.Value {
	var (
		keyType   = typ.Key()
		valueType = typ.Elem()
		resultPtr = reflect.New(reflect.MapOf(keyType, valueType))
		result    = resultPtr.Elem()
	)
	result.Set(reflect.MakeMap(typ))
	if params.JSON != nil {
		// Try to inject the response as a json into the created result
		if err := json.Unmarshal(params.JSON, resultPtr.Interface()); err != nil {
			binderLog.Warn("bindMap: Unable to unmarshal request", "name", name, "error", err)
		}
		return result
	}

	for paramName := range params.Values {
		// The paramName string must start with the value in the "name" parameter,
		// otherwise there is no way the parameter is part of the map
		if !strings.HasPrefix(paramName, name) {
			continue
		}

		suffix := paramName[len(name)+1:]
		fieldName := nextKey(suffix)
		if fieldName != "" {
			fieldName = fieldName[:len(fieldName)-1]
		}
		if !strings.HasPrefix(paramName, name+"["+fieldName+"]") {
			continue
		}

		result.SetMapIndex(BindValue(fieldName, keyType), Bind(params, name+"["+fieldName+"]", valueType))
	}
	return result
}

func unbindMap(output map[string]string, name string, iface interface{}) {
	mapValue := reflect.ValueOf(iface)
	for _, key := range mapValue.MapKeys() {
		Unbind(output, name+"["+fmt.Sprintf("%v", key.Interface())+"]",
			mapValue.MapIndex(key).Interface())
	}
}

// Bind takes the name and type of the desired parameter and constructs it
// from one or more values from Params.
// Returns the zero value of the type upon any sort of failure.
func Bind(params *Params, name string, typ reflect.Type) reflect.Value {
	if binder, found := binderForType(typ); found {
		return binder.Bind(params, name, typ)
	}
	return reflect.Zero(typ)
}

func BindValue(val string, typ reflect.Type) reflect.Value {
	return Bind(&Params{Values: map[string][]string{"": {val}}}, "", typ)
}

func BindFile(fileHeader *multipart.FileHeader, typ reflect.Type) reflect.Value {
	return Bind(&Params{Files: map[string][]*multipart.FileHeader{"": {fileHeader}}}, "", typ)
}

func Unbind(output map[string]string, name string, val interface{}) {
	if binder, found := binderForType(reflect.TypeOf(val)); found {
		if binder.Unbind != nil {
			binder.Unbind(output, name, val)
		} else {
			binderLog.Error("Unbind: Unable to unmarshal request", "name", name, "value", val)
		}
	}
}

func binderForType(typ reflect.Type) (Binder, bool) {
	binder, ok := TypeBinders[typ]
	if !ok {
		binder, ok = KindBinders[typ.Kind()]
		if !ok {
			binderLog.Error("binderForType: no binder for type", "type", typ)
			return Binder{}, false
		}
	}
	return binder, true
}

// Sadly, the binder lookups can not be declared initialized -- that results in
// an "initialization loop" compile error.
func init() {
	KindBinders[reflect.Int] = IntBinder
	KindBinders[reflect.Int8] = IntBinder
	KindBinders[reflect.Int16] = IntBinder
	KindBinders[reflect.Int32] = IntBinder
	KindBinders[reflect.Int64] = IntBinder

	KindBinders[reflect.Uint] = UintBinder
	KindBinders[reflect.Uint8] = UintBinder
	KindBinders[reflect.Uint16] = UintBinder
	KindBinders[reflect.Uint32] = UintBinder
	KindBinders[reflect.Uint64] = UintBinder

	KindBinders[reflect.Float32] = FloatBinder
	KindBinders[reflect.Float64] = FloatBinder

	KindBinders[reflect.String] = StringBinder
	KindBinders[reflect.Bool] = BoolBinder
	KindBinders[reflect.Slice] = Binder{bindSlice, unbindSlice}
	KindBinders[reflect.Struct] = Binder{bindStruct, unbindStruct}
	KindBinders[reflect.Ptr] = PointerBinder
	KindBinders[reflect.Map] = MapBinder

	TypeBinders[reflect.TypeOf(time.Time{})] = TimeBinder

	// Uploads
	TypeBinders[reflect.TypeOf(&os.File{})] = Binder{bindFile, nil}
	TypeBinders[reflect.TypeOf([]byte{})] = Binder{bindByteArray, nil}
	TypeBinders[reflect.TypeOf((*io.Reader)(nil)).Elem()] = Binder{bindReadSeeker, nil}
	TypeBinders[reflect.TypeOf((*io.ReadSeeker)(nil)).Elem()] = Binder{bindReadSeeker, nil}

	OnAppStart(func() {
		DateTimeFormat = Config.StringDefault("format.datetime", DefaultDateTimeFormat)
		DateFormat = Config.StringDefault("format.date", DefaultDateFormat)
		TimeFormats = append(TimeFormats, DateTimeFormat, DateFormat)
	})
}
```

## `binder_test.go` (changed lines (10, 101, 171, 216))

```go
// Copyright (c) 2012-2016 The Revel Framework Authors, All rights reserved.
// Revel Framework source code and usage is governed by a MIT style
// license that can be found in the LICENSE file.

package revel

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"reflect"
	"sort"
	"strings"
	"testing"
	"time"
)

type A struct {
	ID      int
	Name    string
	B       B
	private int
}

type B struct {
	Extra string
}

var (
	ParamTestValues = map[string][]string{
		"int":                            {"1"},
		"int8":                           {"1"},
		"int16":                          {"1"},
		"int32":                          {"1"},
		"int64":                          {"1"},
		"uint":                           {"1"},
		"uint8":                          {"1"},
		"uint16":                         {"1"},
		"uint32":                         {"1"},
		"uint64":                         {"1"},
		"float32":                        {"1.000000"},
		"float64":                        {"1.000000"},
		"str":                            {"hello"},
		"bool-true":                      {"true"},
		"bool-1":                         {"1"},
		"bool-on":                        {"on"},
		"bool-false":                     {"false"},
		"bool-0":                         {"0"},
		"bool-0.0":                       {"0.0"},
		"bool-off":                       {"off"},
		"bool-f":                         {"f"},
		"date":                           {"1982-07-09"},
		"datetime":                       {"1982-07-09 21:30"},
		"customDate":                     {"07/09/1982"},
		"arr[0]":                         {"1"},
		"arr[1]":                         {"2"},
		"arr[3]":                         {"3"},
		"uarr[]":                         {"1", "2"},
		"arruarr[0][]":                   {"1", "2"},
		"arruarr[1][]":                   {"3", "4"},
		"2darr[0][0]":                    {"0"},
		"2darr[0][1]":                    {"1"},
		"2darr[1][0]":                    {"10"},
		"2darr[1][1]":                    {"11"},
		"A.ID":                           {"123"},
		"A.Name":                         {"rob"},
		"B.ID":                           {"123"},
		"B.Name":                         {"rob"},
		"B.B.Extra":                      {"hello"},
		"pB.ID":                          {"123"},
		"pB.Name":                        {"rob"},
		"pB.B.Extra":                     {"hello"},
		"priv.private":                   {"123"},
		"arrC[0].ID":                     {"5"},
		"arrC[0].Name":                   {"rob"},
		"arrC[0].B.Extra":                {"foo"},
		"arrC[1].ID":                     {"8"},
		"arrC[1].Name":                   {"bill"},
		"m[a]":                           {"foo"},
		"m[b]":                           {"bar"},
		"m2[1]":                          {"foo"},
		"m2[2]":                          {"bar"},
		"m3[a]":                          {"1"},
		"m3[b]":                          {"2"},
		"m4[a].ID":                       {"1"},
		"m4[a].Name":                     {"foo"},
		"m4[b].ID":                       {"2"},
		"m4[b].Name":                     {"bar"},
		"mapWithAMuchLongerName[a].ID":   {"1"},
		"mapWithAMuchLongerName[a].Name": {"foo"},
		"mapWithAMuchLongerName[b].ID":   {"2"},
		"mapWithAMuchLongerName[b].Name": {"bar"},
		"invalidInt":                     {"xyz"},
		"invalidInt2":                    {""},
		"invalidBool":                    {"xyz"},
		"invalidArr":                     {"xyz"},
		"int8-overflow":                  {"1024"},
		"uint8-overflow":                 {"1024"},
	}

	testDate     = time.Date(1982, time.July, 9, 0, 0, 0, 0, time.UTC)
	testDatetime = time.Date(1982, time.July, 9, 21, 30, 0, 0, time.UTC)
)

var binderTestCases = map[string]interface{}{
	"int":        1,
	"int8":       int8(1),
	"int16":      int16(1),
	"int32":      int32(1),
	"int64":      int64(1),
	"uint":       1,
	"uint8":      uint8(1),
	"uint16":     uint16(1),
	"uint32":     uint32(1),
	"uint64":     uint64(1),
	"float32":    float32(1.0),
	"float64":    float64(1.0),
	"str":        "hello",
	"bool-true":  true,
	"bool-1":     true,
	"bool-on":    true,
	"bool-false": false,
	"bool-0":     false,
	"bool-0.0":   false,
	"bool-off":   false,
	"bool-f":     false,
	"date":       testDate,
	"datetime":   testDatetime,
	"customDate": testDate,
	"arr":        []int{1, 2, 0, 3},
	"uarr":       []int{1, 2},
	"arruarr":    [][]int{{1, 2}, {3, 4}},
	"2darr":      [][]int{{0, 1}, {10, 11}},
	"A":          A{ID: 123, Name: "rob"},
	"B":          A{ID: 123, Name: "rob", B: B{Extra: "hello"}},
	"pB":         &A{ID: 123, Name: "rob", B: B{Extra: "hello"}},
	"arrC": []A{
		{
			ID:   5,
			Name: "rob",
			B:    B{"foo"},
		},
		{
			ID:   8,
			Name: "bill",
		},
	},
	"m":  map[string]string{"a": "foo", "b": "bar"},
	"m2": map[int]string{1: "foo", 2: "bar"},
	"m3": map[string]int{"a": 1, "b": 2},
	"m4": map[string]A{"a": {ID: 1, Name: "foo"}, "b": {ID: 2, Name: "bar"}},

	// NOTE: We also include a map with a longer name than the others since this has caused problems
	// described in github issue #1285, resolved in pull request #1344. This test case should
	// prevent regression.
	"mapWithAMuchLongerName": map[string]A{"a": {ID: 1, Name: "foo"}, "b": {ID: 2, Name: "bar"}},

	// TODO: Tests that use TypeBinders

	// Invalid value tests (the result should always be the zero value for that type)
	// The point of these is to ensure that invalid user input does not cause panics.
	"invalidInt":     0,
	"invalidInt2":    0,
	"invalidBool":    true,
	"invalidArr":     []int{},
	"priv":           A{},
	"int8-overflow":  int8(0),
	"uint8-overflow": uint8(0),
}

// Types that files may be bound to, and a func that can read the content from
// that type.
// TODO: Is there any way to create a slice, given only the element Type?
var fileBindings = []struct{ val, arrval, f interface{} }{
	{(**os.File)(nil), []*os.File{}, ioutil.ReadAll},
	{(*[]byte)(nil), [][]byte{}, func(b []byte) []byte { return b }},
	{(*io.Reader)(nil), []io.Reader{}, ioutil.ReadAll},
	{(*io.ReadSeeker)(nil), []io.ReadSeeker{}, ioutil.ReadAll},
}

func TestJsonBinder(t *testing.T) {
	// create a structure to be populated
	{
		d, _ := json.Marshal(map[string]int{"a": 1})
		params := &Params{JSON: d}
		foo := struct{ A int }{}
		c := NewTestController(nil, getMultipartRequest())

		ParseParams(params, NewRequest(c.Request.In))
		actual := Bind(params, "test", reflect.TypeOf(foo))
		valEq(t, "TestJsonBinder", reflect.ValueOf(actual.Interface().(struct{ A int }).A), reflect.ValueOf(1))
	}
	{
		d, _ := json.Marshal(map[string]interface{}{"a": map[string]int{"b": 45}})
		params := &Params{JSON: d}
		testMap := map[string]interface{}{}
		actual := Bind(params, "test", reflect.TypeOf(testMap)).Interface().(map[string]interface{})
		if actual["a"].(map[string]interface{})["b"].(float64) != 45 {
			t.Errorf("Failed to fetch map value %#v", actual["a"])
		}
		// Check to see if a named map works
		actualb := Bind(params, "test", reflect.TypeOf(map[string]map[string]float64{})).Interface().(map[string]map[string]float64)
		if actualb["a"]["b"] != 45 {
			t.Errorf("Failed to fetch map value %#v", actual["a"])
		}

	}
}

func TestBinder(t *testing.T) {
	// Reuse the mvc_test.go multipart request to test the binder.
	params := &Params{}
	c := NewTestController(nil, getMultipartRequest())
	ParseParams(params, NewRequest(c.Request.In))
	params.Values = ParamTestValues

	// Values
	for k, v := range binderTestCases {
		actual := Bind(params, k, reflect.TypeOf(v))
		expected := reflect.ValueOf(v)
		valEq(t, k, actual, expected)
	}

	// Files

	// Get the keys in sorted order to make the expectation right.
	keys := []string{}
	for k := range expectedFiles {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	expectedBoundFiles := make(map[string][]fh)
	for _, k := range keys {
		fhs := expectedFiles[k]
		k := nextKey(k)
		expectedBoundFiles[k] = append(expectedBoundFiles[k], fhs...)
	}

	for k, fhs := range expectedBoundFiles {

		if len(fhs) == 1 {
			// Test binding single files to: *os.File, []byte, io.Reader, io.ReadSeeker
			for _, binding := range fileBindings {
				typ := reflect.TypeOf(binding.val).Elem()
				actual := Bind(params, k, typ)
				if !actual.IsValid() || (actual.Kind() == reflect.Interface && actual.IsNil()) {
					t.Errorf("%s (%s) - Returned nil.", k, typ)
					continue
				}
				returns := reflect.ValueOf(binding.f).Call([]reflect.Value{actual})
				valEq(t, k, returns[0], reflect.ValueOf(fhs[0].content))
			}

		} else {
			// Test binding multi to:
			// []*os.File, [][]byte, []io.Reader, []io.ReadSeeker
			for _, binding := range fileBindings {
				typ := reflect.TypeOf(binding.arrval)
				actual := Bind(params, k, typ)
				if actual.Len() != len(fhs) {
					t.Fatalf("%s (%s) - Number of files: (expected) %d != %d (actual)",
						k, typ, len(fhs), actual.Len())
				}
				for i := range fhs {
					returns := reflect.ValueOf(binding.f).Call([]reflect.Value{actual.Index(i)})
					if !returns[0].IsValid() {
						t.Errorf("%s (%s) - Returned nil.", k, typ)
						continue
					}
					valEq(t, k, returns[0], reflect.ValueOf(fhs[i].content))
				}
			}
		}
	}
}

// Unbinding tests

var unbinderTestCases = map[string]interface{}{
	"int":        1,
	"int8":       int8(1),
	"int16":      int16(1),
	"int32":      int32(1),
	"int64":      int64(1),
	"uint":       1,
	"uint8":      uint8(1),
	"uint16":     uint16(1),
	"uint32":     uint32(1),
	"uint64":     uint64(1),
	"float32":    float32(1.0),
	"float64":    float64(1.0),
	"str":        "hello",
	"bool-true":  true,
	"bool-false": false,
	"date":       testDate,
	"datetime":   testDatetime,
	"arr":        []int{1, 2, 0, 3},
	"2darr":      [][]int{{0, 1}, {10, 11}},
	"A":          A{ID: 123, Name: "rob"},
	"B":          A{ID: 123, Name: "rob", B: B{Extra: "hello"}},
	"pB":         &A{ID: 123, Name: "rob", B: B{Extra: "hello"}},
	"arrC": []A{
		{
			ID:   5,
			Name: "rob",
			B:    B{"foo"},
		},
		{
			ID:   8,
			Name: "bill",
		},
	},
	"m":  map[string]string{"a": "foo", "b": "bar"},
	"m2": map[int]string{1: "foo", 2: "bar"},
	"m3": map[string]int{"a": 1, "b": 2},
}

// Some of the unbinding results are not exactly what is in ParamTestValues, since it
// serializes implicit zero values explicitly.
var unbinderOverrideAnswers = map[string]map[string]string{
	"arr": {
		"arr[0]": "1",
		"arr[1]": "2",
		"arr[2]": "0",
		"arr[3]": "3",
	},
	"A": {
		"A.ID":      "123",
		"A.Name":    "rob",
		"A.B.Extra": "",
	},
	"arrC": {
		"arrC[0].ID":      "5",
		"arrC[0].Name":    "rob",
		"arrC[0].B.Extra": "foo",
		"arrC[1].ID":      "8",
		"arrC[1].Name":    "bill",
		"arrC[1].B.Extra": "",
	},
	"m":  {"m[a]": "foo", "m[b]": "bar"},
	"m2": {"m2[1]": "foo", "m2[2]": "bar"},
	"m3": {"m3[a]": "1", "m3[b]": "2"},
}

func TestUnbinder(t *testing.T) {
	for k, v := range unbinderTestCases {
		actual := make(map[string]string)
		Unbind(actual, k, v)

		// Get the expected key/values.
		expected, ok := unbinderOverrideAnswers[k]
		if !ok {
			expected = make(map[string]string)
			for k2, v2 := range ParamTestValues {
				if k == k2 || strings.HasPrefix(k2, k+".") || strings.HasPrefix(k2, k+"[") {
					expected[k2] = v2[0]
				}
			}
		}

		// Compare length and values.
		if len(actual) != len(expected) {
			t.Errorf("Length mismatch\nExpected length %d, actual %d\nExpected: %s\nActual: %s",
				len(expected), len(actual), expected, actual)
		}
		for k, v := range actual {
			if expected[k] != v {
				t.Errorf("Value mismatch.\nExpected: %s\nActual: %s", expected, actual)
			}
		}
	}
}

// Helpers

func valEq(t *testing.T, name string, actual, expected reflect.Value) {
	switch expected.Kind() {
	case reflect.Slice:
		// Check the type/length/element type
		if !eq(t, name+" (type)", actual.Kind(), expected.Kind()) ||
			!eq(t, name+" (len)", actual.Len(), expected.Len()) ||
			!eq(t, name+" (elem)", actual.Type().Elem(), expected.Type().Elem()) {
			return
		}

		// Check value equality for each element.
		for i := 0; i < actual.Len(); i++ {
			valEq(t, fmt.Sprintf("%s[%d]", name, i), actual.Index(i), expected.Index(i))
		}

	case reflect.Ptr:
		// Check equality on the element type.
		valEq(t, name, actual.Elem(), expected.Elem())
	case reflect.Map:
		if !eq(t, name+" (len)", actual.Len(), expected.Len()) {
			return
		}
		for _, key := range expected.MapKeys() {
			expectedValue := expected.MapIndex(key)
			actualValue := actual.MapIndex(key)
			if actualValue.IsValid() {
				valEq(t, fmt.Sprintf("%s[%s]", name, key), actualValue, expectedValue)
			} else {
				t.Errorf("Expected key %s not found", key)
			}
		}
	default:
		eq(t, name, actual.Interface(), expected.Interface())
	}
}

func init() {
	DateFormat = DefaultDateFormat
	DateTimeFormat = DefaultDateTimeFormat
	TimeFormats = append(TimeFormats, DefaultDateFormat, DefaultDateTimeFormat, "01/02/2006")
}
```
