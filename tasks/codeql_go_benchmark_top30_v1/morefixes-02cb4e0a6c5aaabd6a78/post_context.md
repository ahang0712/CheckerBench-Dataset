# Patch-overlapping Go context after the fix

## `docx.go` (changed lines (43,))

```go
package docconv

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"time"
)

type typeOverride struct {
	XMLName     xml.Name `xml:"Override"`
	ContentType string   `xml:"ContentType,attr"`
	PartName    string   `xml:"PartName,attr"`
}

type contentTypeDefinition struct {
	XMLName   xml.Name       `xml:"Types"`
	Overrides []typeOverride `xml:"Override"`
}

// ConvertDocx converts an MS Word docx file to text.
func ConvertDocx(r io.Reader) (string, map[string]string, error) {
	var size int64

	// Common case: if the reader is a file (or trivial wrapper), avoid
	// loading it all into memory.
	var ra io.ReaderAt
	if f, ok := r.(interface {
		io.ReaderAt
		Stat() (os.FileInfo, error)
	}); ok {
		si, err := f.Stat()
		if err != nil {
			return "", nil, err
		}
		size = si.Size()
		ra = f
	} else {
		b, err := ioutil.ReadAll(io.LimitReader(r, maxBytes))
		if err != nil {
			return "", nil, nil
		}
		size = int64(len(b))
		ra = bytes.NewReader(b)
	}

	zr, err := zip.NewReader(ra, size)
	if err != nil {
		return "", nil, fmt.Errorf("error unzipping data: %v", err)
	}

	zipFiles := mapZipFiles(zr.File)

	contentTypeDefinition, err := getContentTypeDefinition(zipFiles["[Content_Types].xml"])
	if err != nil {
		return "", nil, err
	}

	meta := make(map[string]string)
	var textHeader, textBody, textFooter string
	for _, override := range contentTypeDefinition.Overrides {
		f := zipFiles[override.PartName]

		switch {
		case override.ContentType == "application/vnd.openxmlformats-package.core-properties+xml":
			rc, err := f.Open()
			if err != nil {
				return "", nil, fmt.Errorf("error opening '%v' from archive: %v", f.Name, err)
			}
			defer rc.Close()

			meta, err = XMLToMap(rc)
			if err != nil {
				return "", nil, fmt.Errorf("error parsing '%v': %v", f.Name, err)
			}

			if tmp, ok := meta["modified"]; ok {
				if t, err := time.Parse(time.RFC3339, tmp); err == nil {
					meta["ModifiedDate"] = fmt.Sprintf("%d", t.Unix())
				}
			}
			if tmp, ok := meta["created"]; ok {
				if t, err := time.Parse(time.RFC3339, tmp); err == nil {
					meta["CreatedDate"] = fmt.Sprintf("%d", t.Unix())
				}
			}
		case override.ContentType == "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml":
			body, err := parseDocxText(f)
			if err != nil {
				return "", nil, err
			}
			textBody += body + "\n"
		case override.ContentType == "application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml":
			footer, err := parseDocxText(f)
			if err != nil {
				return "", nil, err
			}
			textFooter += footer + "\n"
		case override.ContentType == "application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml":
			header, err := parseDocxText(f)
			if err != nil {
				return "", nil, err
			}
			textHeader += header + "\n"
		}

	}
	return textHeader + "\n" + textBody + "\n" + textFooter, meta, nil
}

func getContentTypeDefinition(zf *zip.File) (*contentTypeDefinition, error) {
	f, err := zf.Open()
	if err != nil {
		return nil, err
	}
	defer f.Close()

	x := &contentTypeDefinition{}
	if err := xml.NewDecoder(f).Decode(x); err != nil {
		return nil, err
	}
	return x, nil
}

func mapZipFiles(files []*zip.File) map[string]*zip.File {
	filesMap := make(map[string]*zip.File, 2*len(files))
	for _, f := range files {
		filesMap[f.Name] = f
		filesMap["/"+f.Name] = f
	}
	return filesMap
}

func parseDocxText(f *zip.File) (string, error) {
	r, err := f.Open()
	if err != nil {
		return "", fmt.Errorf("error opening '%v' from archive: %v", f.Name, err)
	}
	defer r.Close()

	text, err := DocxXMLToText(r)
	if err != nil {
		return "", fmt.Errorf("error parsing '%v': %v", f.Name, err)
	}
	return text, nil
}

// DocxXMLToText converts Docx XML into plain text.
func DocxXMLToText(r io.Reader) (string, error) {
	return XMLToText(r, []string{"br", "p", "tab"}, []string{"instrText", "script"}, true)
}
```

## `limit.go` (changed lines (1, 2, 3))

```go
package docconv

const maxBytes = 20 << 20 // 20MB
```

## `odt.go` (changed lines (17,))

```go
package docconv

import (
	"archive/zip"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"time"
)

// ConvertODT converts a ODT file to text
func ConvertODT(r io.Reader) (string, map[string]string, error) {
	meta := make(map[string]string)
	var textBody string

	b, err := ioutil.ReadAll(io.LimitReader(r, maxBytes))
	if err != nil {
		return "", nil, err
	}
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return "", nil, fmt.Errorf("error unzipping data: %v", err)
	}

	for _, f := range zr.File {
		switch f.Name {
		case "meta.xml":
			rc, err := f.Open()
			if err != nil {
				return "", nil, fmt.Errorf("error extracting '%v' from archive: %v", f.Name, err)
			}
			defer rc.Close()

			info, err := XMLToMap(rc)
			if err != nil {
				return "", nil, fmt.Errorf("error parsing '%v': %v", f.Name, err)
			}

			if tmp, ok := info["creator"]; ok {
				meta["Author"] = tmp
			}
			if tmp, ok := info["date"]; ok {
				if t, err := time.Parse("2006-01-02T15:04:05", tmp); err == nil {
					meta["ModifiedDate"] = fmt.Sprintf("%d", t.Unix())
				}
			}
			if tmp, ok := info["creation-date"]; ok {
				if t, err := time.Parse("2006-01-02T15:04:05", tmp); err == nil {
					meta["CreatedDate"] = fmt.Sprintf("%d", t.Unix())
				}
			}

		case "content.xml":
			rc, err := f.Open()
			if err != nil {
				return "", nil, fmt.Errorf("error extracting '%v' from archive: %v", f.Name, err)
			}
			defer rc.Close()

			textBody, err = XMLToText(rc, []string{"br", "p", "tab"}, []string{}, true)
			if err != nil {
				return "", nil, fmt.Errorf("error parsing '%v': %v", f.Name, err)
			}
		}
	}

	return textBody, meta, nil
}
```

## `pages.go` (changed lines (24,))

```go
package docconv

import (
	"archive/zip"
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"io/ioutil"
	"strings"

	"google.golang.org/protobuf/proto"

	TSP "code.sajari.com/docconv/iWork"
	"code.sajari.com/docconv/snappy"
)

// ConvertPages converts a Pages file to text.
func ConvertPages(r io.Reader) (string, map[string]string, error) {
	meta := make(map[string]string)
	var textBody string

	b, err := ioutil.ReadAll(io.LimitReader(r, maxBytes))
	if err != nil {
		return "", nil, fmt.Errorf("error reading data: %v", err)
	}

	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return "", nil, fmt.Errorf("error unzipping data: %v", err)
	}

	for _, f := range zr.File {
		if strings.HasSuffix(f.Name, "Preview.pdf") {
			// There is a preview PDF version we can use
			if rc, err := f.Open(); err == nil {
				return ConvertPDF(rc)
			}
		}
		if f.Name == "index.xml" {
			// There's an XML version we can use
			if rc, err := f.Open(); err == nil {
				return ConvertXML(rc)
			}
		}
		if f.Name == "Index/Document.iwa" {
			rc, _ := f.Open()
			defer rc.Close()
			bReader := bufio.NewReader(snappy.NewReader(io.MultiReader(strings.NewReader("\xff\x06\x00\x00sNaPpY"), rc)))

			// Ignore error.
			// NOTE: This error was unchecked. Need to revisit this to see if it
			// should be acted on.
			archiveLength, _ := binary.ReadVarint(bReader)

			// Ignore error.
			// NOTE: This error was unchecked. Need to revisit this to see if it
			// should be acted on.
			archiveInfoData, _ := ioutil.ReadAll(io.LimitReader(bReader, archiveLength))

			archiveInfo := &TSP.ArchiveInfo{}
			err = proto.Unmarshal(archiveInfoData, archiveInfo)
			fmt.Println("archiveInfo:", archiveInfo, err)
		}
	}

	return textBody, meta, nil
}
```

## `xml.go` (changed lines (28, 79))

```go
package docconv

import (
	"bytes"
	"encoding/xml"
	"fmt"
	"io"
)

// ConvertXML converts an XML file to text.
func ConvertXML(r io.Reader) (string, map[string]string, error) {
	meta := make(map[string]string)
	cleanXML, err := Tidy(r, true)
	if err != nil {
		return "", nil, fmt.Errorf("tidy error: %v", err)
	}
	result, err := XMLToText(bytes.NewReader(cleanXML), []string{}, []string{}, true)
	if err != nil {
		return "", nil, fmt.Errorf("error from XMLToText: %v", err)
	}
	return result, meta, nil
}

// XMLToText converts XML to plain text given how to treat elements.
func XMLToText(r io.Reader, breaks []string, skip []string, strict bool) (string, error) {
	var result string

	dec := xml.NewDecoder(io.LimitReader(r, maxBytes))
	dec.Strict = strict
	for {
		t, err := dec.Token()
		if err != nil {
			if err == io.EOF {
				break
			}
			return "", err
		}

		switch v := t.(type) {
		case xml.CharData:
			result += string(v)
		case xml.StartElement:
			for _, breakElement := range breaks {
				if v.Name.Local == breakElement {
					result += "\n"
				}
			}
			for _, skipElement := range skip {
				if v.Name.Local == skipElement {
					depth := 1
					for {
						t, err := dec.Token()
						if err != nil {
							// An io.EOF here is actually an error.
							return "", err
						}

						switch t.(type) {
						case xml.StartElement:
							depth++
						case xml.EndElement:
							depth--
						}

						if depth == 0 {
							break
						}
					}
				}
			}
		}
	}
	return result, nil
}

// XMLToMap converts XML to a nested string map.
func XMLToMap(r io.Reader) (map[string]string, error) {
	m := make(map[string]string)
	dec := xml.NewDecoder(io.LimitReader(r, maxBytes))
	var tagName string
	for {
		t, err := dec.Token()
		if err != nil {
			if err == io.EOF {
				break
			}
			return nil, err
		}

		switch v := t.(type) {
		case xml.StartElement:
			tagName = string(v.Name.Local)
		case xml.CharData:
			m[tagName] = string(v)
		}
	}
	return m, nil
}
```
