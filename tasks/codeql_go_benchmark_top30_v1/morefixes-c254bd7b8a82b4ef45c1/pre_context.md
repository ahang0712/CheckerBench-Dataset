# Patch-overlapping Go context before the fix

## `archiver.go` (changed lines (10, 108))

```go
package archiver

import (
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
)

// Archiver represent a archive format
type Archiver interface {
	// Match checks supported files
	Match(filename string) bool
	// Make makes an archive file on disk.
	Make(destination string, sources []string) error
	// Open extracts an archive file on disk.
	Open(source, destination string) error
	// Write writes an archive to a Writer.
	Write(output io.Writer, sources []string) error
	// Read reads an archive from a Reader.
	Read(input io.Reader, destination string) error
}

// SupportedFormats contains all supported archive formats
var SupportedFormats = map[string]Archiver{}

// RegisterFormat adds a supported archive format
func RegisterFormat(name string, format Archiver) {
	if _, ok := SupportedFormats[name]; ok {
		log.Printf("Format %s already exists, skip!\n", name)
		return
	}
	SupportedFormats[name] = format
}

// MatchingFormat returns the first archive format that matches
// the given file, or nil if there is no match
func MatchingFormat(fpath string) Archiver {
	for _, fmt := range SupportedFormats {
		if fmt.Match(fpath) {
			return fmt
		}
	}
	return nil
}

func writeNewFile(fpath string, in io.Reader, fm os.FileMode) error {
	err := os.MkdirAll(filepath.Dir(fpath), 0755)
	if err != nil {
		return fmt.Errorf("%s: making directory for file: %v", fpath, err)
	}

	out, err := os.Create(fpath)
	if err != nil {
		return fmt.Errorf("%s: creating new file: %v", fpath, err)
	}
	defer out.Close()

	err = out.Chmod(fm)
	if err != nil && runtime.GOOS != "windows" {
		return fmt.Errorf("%s: changing file mode: %v", fpath, err)
	}

	_, err = io.Copy(out, in)
	if err != nil {
		return fmt.Errorf("%s: writing file: %v", fpath, err)
	}
	return nil
}

func writeNewSymbolicLink(fpath string, target string) error {
	err := os.MkdirAll(filepath.Dir(fpath), 0755)
	if err != nil {
		return fmt.Errorf("%s: making directory for file: %v", fpath, err)
	}

	err = os.Symlink(target, fpath)
	if err != nil {
		return fmt.Errorf("%s: making symbolic link for: %v", fpath, err)
	}

	return nil
}

func writeNewHardLink(fpath string, target string) error {
	err := os.MkdirAll(filepath.Dir(fpath), 0755)
	if err != nil {
		return fmt.Errorf("%s: making directory for file: %v", fpath, err)
	}

	err = os.Link(target, fpath)
	if err != nil {
		return fmt.Errorf("%s: making hard link for: %v", fpath, err)
	}

	return nil
}

func mkdir(dirPath string) error {
	err := os.MkdirAll(dirPath, 0755)
	if err != nil {
		return fmt.Errorf("%s: making directory: %v", dirPath, err)
	}
	return nil
}
```

## `cmd/archiver/main.go` (changed lines (30, 31))

```go
package main

import (
	"fmt"
	"os"

	"github.com/mholt/archiver"
)

func main() {
	if len(os.Args) < 3 {
		fatal(usage)
	}

	cmd, filename := os.Args[1], os.Args[2]

	ff := archiver.MatchingFormat(filename)
	if ff == nil {
		fatalf("%s: Unsupported file extension", filename)
	}

	var err error
	switch cmd {
	case "make":
		if len(os.Args) < 4 {
			fatal(usage)
		}
		err = ff.Make(filename, os.Args[3:])
	case "open":
		dest := ""
		if len(os.Args) == 4 {
			dest = os.Args[3]
		} else if len(os.Args) > 4 {
			fatal(usage)
		}
		err = ff.Open(filename, dest)
	default:
		fatal(usage)
	}
	if err != nil {
		fatal(err)
	}
}

func fatal(v ...interface{}) {
	fmt.Fprintln(os.Stderr, v...)
	os.Exit(1)
}

func fatalf(s string, v ...interface{}) {
	fmt.Fprintf(os.Stderr, s+"\n", v...)
	os.Exit(1)
}

const usage = `Usage: archiver {make|open} <archive file> [files...]
  make
    Create a new archive file. List the files/folders
    to include in the archive; at least one required.
  open
    Extract an archive file. Give only the archive to
    open and the destination folder to extract into.

  Specifying archive format:
    The format of the archive is determined by its
    file extension. Supported extensions:
      .zip
      .tar
      .tar.gz
      .tgz
      .tar.bz2
      .tbz2
      .tar.xz
      .txz
      .tar.lz4
      .tlz4
      .tar.sz
      .tsz
      .rar (open only)

  Existing files:
    When creating an archive file that already exists,
    archiver will overwrite the existing file. When
    extracting files, archiver will NOT overwrite files
    that already exist in the destination path; this
    is treated as an error and extraction will abort.`
```

## `rar.go` (changed lines (75, 76, 77, 85, 86, 90, 91))

```go
package archiver

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/nwaples/rardecode"
)

// Rar is for RAR archive format
var Rar rarFormat

func init() {
	RegisterFormat("Rar", Rar)
}

type rarFormat struct{}

func (rarFormat) Match(filename string) bool {
	return strings.HasSuffix(strings.ToLower(filename), ".rar") || isRar(filename)
}

// isRar checks the file has the RAR 1.5 or 5.0 format signature by reading its
// beginning bytes and matching it
func isRar(rarPath string) bool {
	f, err := os.Open(rarPath)
	if err != nil {
		return false
	}
	defer f.Close()

	buf := make([]byte, 8)
	if n, err := f.Read(buf); err != nil || n < 8 {
		return false
	}

	return bytes.Equal(buf[:7], []byte("Rar!\x1a\x07\x00")) || // ver 1.5
		bytes.Equal(buf, []byte("Rar!\x1a\x07\x01\x00")) // ver 5.0
}

// Write outputs a .rar archive, but this is not implemented because
// RAR is a proprietary format. It is here only for symmetry with
// the other archive formats in this package.
func (rarFormat) Write(output io.Writer, filePaths []string) error {
	return fmt.Errorf("write: RAR not implemented (proprietary format)")
}

// Make makes a .rar archive, but this is not implemented because
// RAR is a proprietary format. It is here only for symmetry with
// the other archive formats in this package.
func (rarFormat) Make(rarPath string, filePaths []string) error {
	return fmt.Errorf("make %s: RAR not implemented (proprietary format)", rarPath)
}

// Read extracts the RAR file read from input and puts the contents
// into destination.
func (rarFormat) Read(input io.Reader, destination string) error {
	rr, err := rardecode.NewReader(input, "")
	if err != nil {
		return fmt.Errorf("read: failed to create reader: %v", err)
	}

	for {
		header, err := rr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			return err
		}

		if header.IsDir {
			err = mkdir(filepath.Join(destination, header.Name))
			if err != nil {
				return err
			}
			continue
		}

		// if files come before their containing folders, then we must
		// create their folders before writing the file
		err = mkdir(filepath.Dir(filepath.Join(destination, header.Name)))
		if err != nil {
			return err
		}

		err = writeNewFile(filepath.Join(destination, header.Name), rr, header.Mode())
		if err != nil {
			return err
		}
	}

	return nil
}

// Open extracts the RAR file at source and puts the contents
// into destination.
func (rarFormat) Open(source, destination string) error {
	rf, err := os.Open(source)
	if err != nil {
		return fmt.Errorf("%s: failed to open file: %v", source, err)
	}
	defer rf.Close()

	return Rar.Read(rf, destination)
}
```

## `tar.go` (changed lines (222, 224, 225, 226, 227, 228, 229, 230, 231))

```go
package archiver

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Tar is for Tar format
var Tar tarFormat

func init() {
	RegisterFormat("Tar", Tar)
}

type tarFormat struct{}

func (tarFormat) Match(filename string) bool {
	return strings.HasSuffix(strings.ToLower(filename), ".tar") || isTar(filename)
}

const tarBlockSize int = 512

// isTar checks the file has the Tar format header by reading its beginning
// block.
func isTar(tarPath string) bool {
	f, err := os.Open(tarPath)
	if err != nil {
		return false
	}
	defer f.Close()

	buf := make([]byte, tarBlockSize)
	if _, err = io.ReadFull(f, buf); err != nil {
		return false
	}

	return hasTarHeader(buf)
}

// hasTarHeader checks passed bytes has a valid tar header or not. buf must
// contain at least 512 bytes and if not, it always returns false.
func hasTarHeader(buf []byte) bool {
	if len(buf) < tarBlockSize {
		return false
	}

	b := buf[148:156]
	b = bytes.Trim(b, " \x00") // clean up all spaces and null bytes
	if len(b) == 0 {
		return false // unknown format
	}
	hdrSum, err := strconv.ParseUint(string(b), 8, 64)
	if err != nil {
		return false
	}

	// According to the go official archive/tar, Sun tar uses signed byte
	// values so this calcs both signed and unsigned
	var usum uint64
	var sum int64
	for i, c := range buf {
		if 148 <= i && i < 156 {
			c = ' ' // checksum field itself is counted as branks
		}
		usum += uint64(uint8(c))
		sum += int64(int8(c))
	}

	if hdrSum != usum && int64(hdrSum) != sum {
		return false // invalid checksum
	}

	return true
}

// Write outputs a .tar file to a Writer containing the
// contents of files listed in filePaths. File paths can
// be those of regular files or directories. Regular
// files are stored at the 'root' of the archive, and
// directories are recursively added.
func (tarFormat) Write(output io.Writer, filePaths []string) error {
	return writeTar(filePaths, output, "")
}

// Make creates a .tar file at tarPath containing the
// contents of files listed in filePaths. File paths can
// be those of regular files or directories. Regular
// files are stored at the 'root' of the archive, and
// directories are recursively added.
func (tarFormat) Make(tarPath string, filePaths []string) error {
	out, err := os.Create(tarPath)
	if err != nil {
		return fmt.Errorf("error creating %s: %v", tarPath, err)
	}
	defer out.Close()

	return writeTar(filePaths, out, tarPath)
}

func writeTar(filePaths []string, output io.Writer, dest string) error {
	tarWriter := tar.NewWriter(output)
	defer tarWriter.Close()

	return tarball(filePaths, tarWriter, dest)
}

// tarball writes all files listed in filePaths into tarWriter, which is
// writing into a file located at dest.
func tarball(filePaths []string, tarWriter *tar.Writer, dest string) error {
	for _, fpath := range filePaths {
		err := tarFile(tarWriter, fpath, dest)
		if err != nil {
			return err
		}
	}
	return nil
}

// tarFile writes the file at source into tarWriter. It does so
// recursively for directories.
func tarFile(tarWriter *tar.Writer, source, dest string) error {
	sourceInfo, err := os.Stat(source)
	if err != nil {
		return fmt.Errorf("%s: stat: %v", source, err)
	}

	var baseDir string
	if sourceInfo.IsDir() {
		baseDir = filepath.Base(source)
	}

	return filepath.Walk(source, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("error walking to %s: %v", path, err)
		}

		header, err := tar.FileInfoHeader(info, path)
		if err != nil {
			return fmt.Errorf("%s: making header: %v", path, err)
		}

		if baseDir != "" {
			header.Name = filepath.Join(baseDir, strings.TrimPrefix(path, source))
		}

		if header.Name == dest {
			// our new tar file is inside the directory being archived; skip it
			return nil
		}

		if info.IsDir() {
			header.Name += "/"
		}

		err = tarWriter.WriteHeader(header)
		if err != nil {
			return fmt.Errorf("%s: writing header: %v", path, err)
		}

		if info.IsDir() {
			return nil
		}

		if header.Typeflag == tar.TypeReg {
			file, err := os.Open(path)
			if err != nil {
				return fmt.Errorf("%s: open: %v", path, err)
			}
			defer file.Close()

			_, err = io.CopyN(tarWriter, file, info.Size())
			if err != nil && err != io.EOF {
				return fmt.Errorf("%s: copying contents: %v", path, err)
			}
		}
		return nil
	})
}

// Read untars a .tar file read from a Reader and puts
// the contents into destination.
func (tarFormat) Read(input io.Reader, destination string) error {
	return untar(tar.NewReader(input), destination)
}

// Open untars source and puts the contents into destination.
func (tarFormat) Open(source, destination string) error {
	f, err := os.Open(source)
	if err != nil {
		return fmt.Errorf("%s: failed to open archive: %v", source, err)
	}
	defer f.Close()

	return Tar.Read(f, destination)
}

// untar un-tarballs the contents of tr into destination.
func untar(tr *tar.Reader, destination string) error {
	for {
		header, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			return err
		}

		if err := untarFile(tr, header, destination); err != nil {
			return err
		}
	}
	return nil
}

// untarFile untars a single file from tr with header header into destination.
func untarFile(tr *tar.Reader, header *tar.Header, destination string) error {
	switch header.Typeflag {
	case tar.TypeDir:
		return mkdir(filepath.Join(destination, header.Name))
	case tar.TypeReg, tar.TypeRegA, tar.TypeChar, tar.TypeBlock, tar.TypeFifo:
		return writeNewFile(filepath.Join(destination, header.Name), tr, header.FileInfo().Mode())
	case tar.TypeSymlink:
		return writeNewSymbolicLink(filepath.Join(destination, header.Name), header.Linkname)
	case tar.TypeLink:
		return writeNewHardLink(filepath.Join(destination, header.Name), filepath.Join(destination, header.Linkname))
	default:
		return fmt.Errorf("%s: unknown type flag: %c", header.Name, header.Typeflag)
	}
}
```

## `zip.go` (changed lines (190,))

```go
// Package archiver makes it super easy to create and open .zip,
// .tar.gz, and .tar.bz2 files.
package archiver

import (
	"archive/zip"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strings"
)

// Zip is for Zip format
var Zip zipFormat

func init() {
	RegisterFormat("Zip", Zip)
}

type zipFormat struct{}

func (zipFormat) Match(filename string) bool {
	return strings.HasSuffix(strings.ToLower(filename), ".zip") || isZip(filename)
}

// isZip checks the file has the Zip format signature by reading its beginning
// bytes and matching it against "PK\x03\x04"
func isZip(zipPath string) bool {
	f, err := os.Open(zipPath)
	if err != nil {
		return false
	}
	defer f.Close()

	buf := make([]byte, 4)
	if n, err := f.Read(buf); err != nil || n < 4 {
		return false
	}

	return bytes.Equal(buf, []byte("PK\x03\x04"))
}

// Write outputs a .zip file to the given writer with
// the contents of files listed in filePaths. File paths
// can be those of regular files or directories. Regular
// files are stored at the 'root' of the archive, and
// directories are recursively added.
//
// Files with an extension for formats that are already
// compressed will be stored only, not compressed.
func (zipFormat) Write(output io.Writer, filePaths []string) error {
	w := zip.NewWriter(output)
	for _, fpath := range filePaths {
		if err := zipFile(w, fpath); err != nil {
			w.Close()
			return err
		}
	}

	return w.Close()
}

// Make creates a .zip file in the location zipPath containing
// the contents of files listed in filePaths. File paths
// can be those of regular files or directories. Regular
// files are stored at the 'root' of the archive, and
// directories are recursively added.
//
// Files with an extension for formats that are already
// compressed will be stored only, not compressed.
func (zipFormat) Make(zipPath string, filePaths []string) error {
	out, err := os.Create(zipPath)
	if err != nil {
		return fmt.Errorf("error creating %s: %v", zipPath, err)
	}
	defer out.Close()

	return Zip.Write(out, filePaths)
}

func zipFile(w *zip.Writer, source string) error {
	sourceInfo, err := os.Stat(source)
	if err != nil {
		return fmt.Errorf("%s: stat: %v", source, err)
	}

	var baseDir string
	if sourceInfo.IsDir() {
		baseDir = filepath.Base(source)
	}

	return filepath.Walk(source, func(fpath string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("walking to %s: %v", fpath, err)
		}

		header, err := zip.FileInfoHeader(info)
		if err != nil {
			return fmt.Errorf("%s: getting header: %v", fpath, err)
		}

		if baseDir != "" {
			name, err := filepath.Rel(source, fpath)
			if err != nil {
				return err
			}
			header.Name = path.Join(baseDir, filepath.ToSlash(name))
		}

		if info.IsDir() {
			header.Name += "/"
			header.Method = zip.Store
		} else {
			ext := strings.ToLower(path.Ext(header.Name))
			if _, ok := compressedFormats[ext]; ok {
				header.Method = zip.Store
			} else {
				header.Method = zip.Deflate
			}
		}

		writer, err := w.CreateHeader(header)
		if err != nil {
			return fmt.Errorf("%s: making header: %v", fpath, err)
		}

		if info.IsDir() {
			return nil
		}

		if header.Mode().IsRegular() {
			file, err := os.Open(fpath)
			if err != nil {
				return fmt.Errorf("%s: opening: %v", fpath, err)
			}
			defer file.Close()

			_, err = io.CopyN(writer, file, info.Size())
			if err != nil && err != io.EOF {
				return fmt.Errorf("%s: copying contents: %v", fpath, err)
			}
		}

		return nil
	})
}

// Read unzips the .zip file read from the input Reader into destination.
func (zipFormat) Read(input io.Reader, destination string) error {
	buf, err := ioutil.ReadAll(input)
	if err != nil {
		return err
	}

	rdr := bytes.NewReader(buf)
	r, err := zip.NewReader(rdr, rdr.Size())
	if err != nil {
		return err
	}

	return unzipAll(r, destination)
}

// Open unzips the .zip file at source into destination.
func (zipFormat) Open(source, destination string) error {
	r, err := zip.OpenReader(source)
	if err != nil {
		return err
	}
	defer r.Close()

	return unzipAll(&r.Reader, destination)
}

func unzipAll(r *zip.Reader, destination string) error {
	for _, zf := range r.File {
		if err := unzipFile(zf, destination); err != nil {
			return err
		}
	}

	return nil
}

func unzipFile(zf *zip.File, destination string) error {
	if strings.HasSuffix(zf.Name, "/") {
		return mkdir(filepath.Join(destination, zf.Name))
	}

	rc, err := zf.Open()
	if err != nil {
		return fmt.Errorf("%s: open compressed file: %v", zf.Name, err)
	}
	defer rc.Close()

	return writeNewFile(filepath.Join(destination, zf.Name), rc, zf.FileInfo().Mode())
}

// compressedFormats is a (non-exhaustive) set of lowercased
// file extensions for formats that are typically already
// compressed. Compressing already-compressed files often
// results in a larger file, so when possible, we check this
// set to avoid that.
var compressedFormats = map[string]struct{}{
	".7z":   {},
	".avi":  {},
	".bz2":  {},
	".cab":  {},
	".gif":  {},
	".gz":   {},
	".jar":  {},
	".jpeg": {},
	".jpg":  {},
	".lz":   {},
	".lzma": {},
	".mov":  {},
	".mp3":  {},
	".mp4":  {},
	".mpeg": {},
	".mpg":  {},
	".png":  {},
	".rar":  {},
	".tbz2": {},
	".tgz":  {},
	".txz":  {},
	".xz":   {},
	".zip":  {},
	".zipx": {},
}
```
