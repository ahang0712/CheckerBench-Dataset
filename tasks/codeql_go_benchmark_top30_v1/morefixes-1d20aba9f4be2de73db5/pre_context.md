# Patch-overlapping Go context before the fix

## `internal/sync/singleflight/singleflight_test.go` (changed lines (17, 30, 44, 92))

```go
// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package singleflight

import (
	"errors"
	"fmt"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestDo(t *testing.T) {
	var g Group
	v, err, _ := g.Do("key", func() (interface{}, error) {
		return "bar", nil
	})
	if got, want := fmt.Sprintf("%v (%T)", v, v), "bar (string)"; got != want {
		t.Errorf("Do = %v; want %v", got, want)
	}
	if err != nil {
		t.Errorf("Do error = %v", err)
	}
}

func TestDoErr(t *testing.T) {
	var g Group
	someErr := errors.New("Some error")
	v, err, _ := g.Do("key", func() (interface{}, error) {
		return nil, someErr
	})
	if err != someErr {
		t.Errorf("Do error = %v; want someErr %v", err, someErr)
	}
	if v != nil {
		t.Errorf("unexpected non-nil value %#v", v)
	}
}

func TestDoDupSuppress(t *testing.T) {
	var g Group
	var wg1, wg2 sync.WaitGroup
	c := make(chan string, 1)
	var calls int32
	fn := func() (interface{}, error) {
		if atomic.AddInt32(&calls, 1) == 1 {
			// First invocation.
			wg1.Done()
		}
		v := <-c
		c <- v // pump; make available for any future calls

		time.Sleep(10 * time.Millisecond) // let more goroutines enter Do

		return v, nil
	}

	const n = 10
	wg1.Add(1)
	for i := 0; i < n; i++ {
		wg1.Add(1)
		wg2.Add(1)
		go func() {
			defer wg2.Done()
			wg1.Done()
			v, err, _ := g.Do("key", fn)
			if err != nil {
				t.Errorf("Do error: %v", err)
				return
			}
			if s, _ := v.(string); s != "bar" {
				t.Errorf("Do = %T %v; want %q", v, v, "bar")
			}
		}()
	}
	wg1.Wait()
	// At least one goroutine is in fn now and all of them have at
	// least reached the line before the Do.
	c <- "bar"
	wg2.Wait()
	if got := atomic.LoadInt32(&calls); got <= 0 || got >= n {
		t.Errorf("number of calls = %d; want over 0 and less than %d", got, n)
	}
}

// Test that singleflight behaves correctly after Forget called.
// See https://github.com/golang/go/issues/31420
func TestForget(t *testing.T) {
	var g Group

	var firstStarted, firstFinished sync.WaitGroup

	firstStarted.Add(1)
	firstFinished.Add(1)

	firstCh := make(chan struct{})
	go func() {
		g.Do("key", func() (i interface{}, e error) {
			firstStarted.Done()
			<-firstCh
			firstFinished.Done()
			return
		})
	}()

	firstStarted.Wait()
	g.Forget("key") // from this point no two function using same key should be executed concurrently

	var secondStarted int32
	var secondFinished int32
	var thirdStarted int32

	secondCh := make(chan struct{})
	secondRunning := make(chan struct{})
	go func() {
		g.Do("key", func() (i interface{}, e error) {
			defer func() {
			}()
			atomic.AddInt32(&secondStarted, 1)
			// Notify that we started
			secondCh <- struct{}{}
			// Wait other get above signal
			<-secondRunning
			<-secondCh
			atomic.AddInt32(&secondFinished, 1)
			return 2, nil
		})
	}()

	close(firstCh)
	firstFinished.Wait() // wait for first execution (which should not affect execution after Forget)

	<-secondCh
	// Notify second that we got the signal that it started
	secondRunning <- struct{}{}
	if atomic.LoadInt32(&secondStarted) != 1 {
		t.Fatal("Second execution should be executed due to usage of forget")
	}

	if atomic.LoadInt32(&secondFinished) == 1 {
		t.Fatal("Second execution should be still active")
	}

	close(secondCh)
	result, _, _ := g.Do("key", func() (i interface{}, e error) {
		atomic.AddInt32(&thirdStarted, 1)
		return 3, nil
	})

	if atomic.LoadInt32(&thirdStarted) != 0 {
		t.Error("Third call should not be started because was started during second execution")
	}
	if result != 2 {
		t.Errorf("We should receive result produced by second call, expected: 2, got %d", result)
	}
}
```

## `service/s3/s3crypto/aes_cbc_content_cipher.go` (changed lines (18, 20))

```go
package s3crypto

import (
	"io"
	"strings"
)

const (
	cbcKeySize   = 32
	cbcNonceSize = 16
)

type cbcContentCipherBuilder struct {
	generator CipherDataGenerator
	padder    Padder
}

// AESCBCContentCipherBuilder returns a new encryption only mode structure with a specific cipher
// for the master key
func AESCBCContentCipherBuilder(generator CipherDataGenerator, padder Padder) ContentCipherBuilder {
	return cbcContentCipherBuilder{generator: generator, padder: padder}
}

func (builder cbcContentCipherBuilder) ContentCipher() (ContentCipher, error) {
	cd, err := builder.generator.GenerateCipherData(cbcKeySize, cbcNonceSize)
	if err != nil {
		return nil, err
	}

	cd.Padder = builder.padder
	return newAESCBCContentCipher(cd)
}

// newAESCBCContentCipher will create a new aes cbc content cipher. If the cipher data's
// will set the CEK algorithm if it hasn't been set.
func newAESCBCContentCipher(cd CipherData) (ContentCipher, error) {
	if len(cd.CEKAlgorithm) == 0 {
		cd.CEKAlgorithm = strings.Join([]string{AESCBC, cd.Padder.Name()}, "/")
	}
	cipher, err := newAESCBC(cd, cd.Padder)
	if err != nil {
		return nil, err
	}

	return &aesCBCContentCipher{
		CipherData: cd,
		Cipher:     cipher,
	}, nil
}

// aesCBCContentCipher will use AES CBC for the main cipher.
type aesCBCContentCipher struct {
	CipherData CipherData
	Cipher     Cipher
}

// EncryptContents will generate a random key and iv and encrypt the data using cbc
func (cc *aesCBCContentCipher) EncryptContents(src io.Reader) (io.Reader, error) {
	return cc.Cipher.Encrypt(src), nil
}

// DecryptContents will use the symmetric key provider to instantiate a new CBC cipher.
// We grab a decrypt reader from CBC and wrap it in a CryptoReadCloser. The only error
// expected here is when the key or iv is of invalid length.
func (cc *aesCBCContentCipher) DecryptContents(src io.ReadCloser) (io.ReadCloser, error) {
	reader := cc.Cipher.Decrypt(src)
	return &CryptoReadCloser{Body: src, Decrypter: reader}, nil
}

// GetCipherData returns cipher data
func (cc aesCBCContentCipher) GetCipherData() CipherData {
	return cc.CipherData
}
```

## `service/s3/s3crypto/aes_gcm_content_cipher.go` (changed lines (18, 32, 33, 34, 35))

```go
package s3crypto

import (
	"io"

	"github.com/aws/aws-sdk-go/aws"
)

const (
	gcmKeySize   = 32
	gcmNonceSize = 12
)

type gcmContentCipherBuilder struct {
	generator CipherDataGenerator
}

// AESGCMContentCipherBuilder returns a new encryption only mode structure with a specific cipher
// for the master key
func AESGCMContentCipherBuilder(generator CipherDataGenerator) ContentCipherBuilder {
	return gcmContentCipherBuilder{generator}
}

func (builder gcmContentCipherBuilder) ContentCipher() (ContentCipher, error) {
	return builder.ContentCipherWithContext(aws.BackgroundContext())
}

func (builder gcmContentCipherBuilder) ContentCipherWithContext(ctx aws.Context) (ContentCipher, error) {
	var cd CipherData
	var err error

	if v, ok := builder.generator.(CipherDataGeneratorWithContext); ok {
		cd, err = v.GenerateCipherDataWithContext(ctx, gcmKeySize, gcmNonceSize)
	} else {
		cd, err = builder.generator.GenerateCipherData(gcmKeySize, gcmNonceSize)
	}
	if err != nil {
		return nil, err
	}

	return newAESGCMContentCipher(cd)
}

func newAESGCMContentCipher(cd CipherData) (ContentCipher, error) {
	cd.CEKAlgorithm = AESGCMNoPadding
	cd.TagLength = "128"

	cipher, err := newAESGCM(cd)
	if err != nil {
		return nil, err
	}

	return &aesGCMContentCipher{
		CipherData: cd,
		Cipher:     cipher,
	}, nil
}

// AESGCMContentCipher will use AES GCM for the main cipher.
type aesGCMContentCipher struct {
	CipherData CipherData
	Cipher     Cipher
}

// EncryptContents will generate a random key and iv and encrypt the data using cbc
func (cc *aesGCMContentCipher) EncryptContents(src io.Reader) (io.Reader, error) {
	return cc.Cipher.Encrypt(src), nil
}

// DecryptContents will use the symmetric key provider to instantiate a new GCM cipher.
// We grab a decrypt reader from gcm and wrap it in a CryptoReadCloser. The only error
// expected here is when the key or iv is of invalid length.
func (cc *aesGCMContentCipher) DecryptContents(src io.ReadCloser) (io.ReadCloser, error) {
	reader := cc.Cipher.Decrypt(src)
	return &CryptoReadCloser{Body: src, Decrypter: reader}, nil
}

// GetCipherData returns cipher data
func (cc aesGCMContentCipher) GetCipherData() CipherData {
	return cc.CipherData
}
```

## `service/s3/s3crypto/aes_gcm_content_cipher_test.go` (changed lines (6, 29))

```go
package s3crypto_test

import (
	"testing"

	"github.com/aws/aws-sdk-go/service/s3/s3crypto"
)

func TestAESGCMContentCipherBuilder(t *testing.T) {
	generator := mockGenerator{}
	if builder := s3crypto.AESGCMContentCipherBuilder(generator); builder == nil {
		t.Error("expected non-nil value")
	}
}

func TestAESGCMContentCipherNewEncryptor(t *testing.T) {
	generator := mockGenerator{}
	builder := s3crypto.AESGCMContentCipherBuilder(generator)
	cipher, err := builder.ContentCipher()

	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}

	if cipher == nil {
		t.Errorf("expected non-nil vaue")
	}
}
```

## `service/s3/s3crypto/aes_gcm_test.go` (changed lines (1, 6, 18, 19, 27, 28, 36, 37, 45, 46, 50, 108, 125, 126, 129, 130))

```go
package s3crypto

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"strings"
	"testing"
)

// AES GCM
func TestAES_GCM_NIST_gcmEncryptExtIV256_PTLen_128_Test_0(t *testing.T) {
	iv, _ := hex.DecodeString("0d18e06c7c725ac9e362e1ce")
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	plaintext, _ := hex.DecodeString("2db5168e932556f8089a0622981d017d")
	expected, _ := hex.DecodeString("fa4362189661d163fcd6a56d8bf0405ad636ac1bbedd5cc3ee727dc2ab4a9489")
	tag, _ := hex.DecodeString("d636ac1bbedd5cc3ee727dc2ab4a9489")
	aesgcmTest(t, iv, key, plaintext, expected, tag)
}

func TestAES_GCM_NIST_gcmEncryptExtIV256_PTLen_104_Test_3(t *testing.T) {
	iv, _ := hex.DecodeString("4742357c335913153ff0eb0f")
	key, _ := hex.DecodeString("e5a0eb92cc2b064e1bc80891faf1fab5e9a17a9c3a984e25416720e30e6c2b21")
	plaintext, _ := hex.DecodeString("8499893e16b0ba8b007d54665a")
	expected, _ := hex.DecodeString("eb8e6175f1fe38eb1acf95fd5188a8b74bb74fda553e91020a23deed45")
	tag, _ := hex.DecodeString("88a8b74bb74fda553e91020a23deed45")
	aesgcmTest(t, iv, key, plaintext, expected, tag)
}

func TestAES_GCM_NIST_gcmEncryptExtIV256_PTLen_256_Test_6(t *testing.T) {
	iv, _ := hex.DecodeString("a291484c3de8bec6b47f525f")
	key, _ := hex.DecodeString("37f39137416bafde6f75022a7a527cc593b6000a83ff51ec04871a0ff5360e4e")
	plaintext, _ := hex.DecodeString("fafd94cede8b5a0730394bec68a8e77dba288d6ccaa8e1563a81d6e7ccc7fc97")
	expected, _ := hex.DecodeString("44dc868006b21d49284016565ffb3979cc4271d967628bf7cdaf86db888e92e501a2b578aa2f41ec6379a44a31cc019c")
	tag, _ := hex.DecodeString("01a2b578aa2f41ec6379a44a31cc019c")
	aesgcmTest(t, iv, key, plaintext, expected, tag)
}

func TestAES_GCM_NIST_gcmEncryptExtIV256_PTLen_408_Test_8(t *testing.T) {
	iv, _ := hex.DecodeString("92f258071d79af3e63672285")
	key, _ := hex.DecodeString("595f259c55abe00ae07535ca5d9b09d6efb9f7e9abb64605c337acbd6b14fc7e")
	plaintext, _ := hex.DecodeString("a6fee33eb110a2d769bbc52b0f36969c287874f665681477a25fc4c48015c541fbe2394133ba490a34ee2dd67b898177849a91")
	expected, _ := hex.DecodeString("bbca4a9e09ae9690c0f6f8d405e53dccd666aa9c5fa13c8758bc30abe1ddd1bcce0d36a1eaaaaffef20cd3c5970b9673f8a65c26ccecb9976fd6ac9c2c0f372c52c821")
	tag, _ := hex.DecodeString("26ccecb9976fd6ac9c2c0f372c52c821")
	aesgcmTest(t, iv, key, plaintext, expected, tag)
}

func TestGCMEncryptReader_SourceError(t *testing.T) {
	gcm := &gcmEncryptReader{
		encrypter: &mockCipherAEAD{},
		src:       &mockSourceReader{err: fmt.Errorf("test read error")},
	}

	b := make([]byte, 10)
	n, err := gcm.Read(b)
	if err == nil {
		t.Fatalf("expected error, but got nil")
	} else if err != nil && !strings.Contains(err.Error(), "test read error") {
		t.Fatalf("expected source read error, but got %v", err)
	}

	if n != 0 {
		t.Errorf("expected number of read bytes to be zero, but got %v", n)
	}
}

func TestGCMDecryptReader_SourceError(t *testing.T) {
	gcm := &gcmDecryptReader{
		decrypter: &mockCipherAEAD{},
		src:       &mockSourceReader{err: fmt.Errorf("test read error")},
	}

	b := make([]byte, 10)
	n, err := gcm.Read(b)
	if err == nil {
		t.Fatalf("expected error, but got nil")
	} else if err != nil && !strings.Contains(err.Error(), "test read error") {
		t.Fatalf("expected source read error, but got %v", err)
	}

	if n != 0 {
		t.Errorf("expected number of read bytes to be zero, but got %v", n)
	}
}

func TestGCMDecryptReader_DecrypterOpenError(t *testing.T) {
	gcm := &gcmDecryptReader{
		decrypter: &mockCipherAEAD{openError: fmt.Errorf("test open error")},
		src:       &mockSourceReader{err: io.EOF},
	}

	b := make([]byte, 10)
	n, err := gcm.Read(b)
	if err == nil {
		t.Fatalf("expected error, but got nil")
	} else if err != nil && !strings.Contains(err.Error(), "test open error") {
		t.Fatalf("expected source read error, but got %v", err)
	}

	if n != 0 {
		t.Errorf("expected number of read bytes to be zero, but got %v", n)
	}
}

func aesgcmTest(t *testing.T, iv, key, plaintext, expected, tag []byte) {
	cd := CipherData{
		Key: key,
		IV:  iv,
	}
	gcm, err := newAESGCM(cd)
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}

	cipherdata := gcm.Encrypt(bytes.NewReader(plaintext))

	ciphertext, err := ioutil.ReadAll(cipherdata)
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}

	// splitting tag and ciphertext
	etag := ciphertext[len(ciphertext)-16:]
	if !bytes.Equal(etag, tag) {
		t.Errorf("expected tags to be equivalent")
	}
	if !bytes.Equal(ciphertext, expected) {
		t.Errorf("expected ciphertext to be equivalent")
	}

	data := gcm.Decrypt(bytes.NewReader(ciphertext))
	text, err := ioutil.ReadAll(data)
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if !bytes.Equal(plaintext, text) {
		t.Errorf("expected ciphertext to be equivalent")
	}
}

type mockSourceReader struct {
	n   int
	err error
}

func (b mockSourceReader) Read(p []byte) (n int, err error) {
	return b.n, b.err
}

type mockCipherAEAD struct {
	seal      []byte
	openError error
}

func (m mockCipherAEAD) NonceSize() int {
	panic("implement me")
}

func (m mockCipherAEAD) Overhead() int {
	panic("implement me")
}

func (m mockCipherAEAD) Seal(dst, nonce, plaintext, additionalData []byte) []byte {
	return m.seal
}

func (m mockCipherAEAD) Open(dst, nonce, ciphertext, additionalData []byte) ([]byte, error) {
	return []byte("mocked decrypt"), m.openError
}
```

## `service/s3/s3crypto/cipher_util.go` (changed lines (6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94))

```go
package s3crypto

import (
	"encoding/base64"
	"strconv"
	"strings"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/awserr"
)

func (client *DecryptionClient) contentCipherFromEnvelope(ctx aws.Context, env Envelope) (ContentCipher, error) {
	wrap, err := client.wrapFromEnvelope(env)
	if err != nil {
		return nil, err
	}

	return client.cekFromEnvelope(ctx, env, wrap)
}

func (client *DecryptionClient) wrapFromEnvelope(env Envelope) (CipherDataDecrypter, error) {
	f, ok := client.WrapRegistry[env.WrapAlg]
	if !ok || f == nil {
		return nil, awserr.New(
			"InvalidWrapAlgorithmError",
			"wrap algorithm isn't supported, "+env.WrapAlg,
			nil,
		)
	}
	return f(env)
}

// AESGCMNoPadding is the constant value that is used to specify
// the CEK algorithm consiting of AES GCM with no padding.
const AESGCMNoPadding = "AES/GCM/NoPadding"

// AESCBC is the string constant that signifies the AES CBC algorithm cipher.
const AESCBC = "AES/CBC"

func (client *DecryptionClient) cekFromEnvelope(ctx aws.Context, env Envelope, decrypter CipherDataDecrypter) (ContentCipher, error) {
	f, ok := client.CEKRegistry[env.CEKAlg]
	if !ok || f == nil {
		return nil, awserr.New(
			"InvalidCEKAlgorithmError",
			"cek algorithm isn't supported, "+env.CEKAlg,
			nil,
		)
	}

	key, err := base64.StdEncoding.DecodeString(env.CipherKey)
	if err != nil {
		return nil, err
	}

	iv, err := base64.StdEncoding.DecodeString(env.IV)
	if err != nil {
		return nil, err
	}

	if d, ok := decrypter.(CipherDataDecrypterWithContext); ok {
		key, err = d.DecryptKeyWithContext(ctx, key)
	} else {
		key, err = decrypter.DecryptKey(key)
	}

	if err != nil {
		return nil, err
	}

	cd := CipherData{
		Key:          key,
		IV:           iv,
		CEKAlgorithm: env.CEKAlg,
		Padder:       client.getPadder(env.CEKAlg),
	}
	return f(cd)
}

// getPadder will return an unpadder with checking the cek algorithm specific padder.
// If there wasn't a cek algorithm specific padder, we check the padder itself.
// We return a no unpadder, if no unpadder was found. This means any customization
// either contained padding within the cipher implementation, and to maintain
// backwards compatility we will simply not unpad anything.
func (client *DecryptionClient) getPadder(cekAlg string) Padder {
	padder, ok := client.PadderRegistry[cekAlg]
	if !ok {
		padder, ok = client.PadderRegistry[cekAlg[strings.LastIndex(cekAlg, "/")+1:]]
		if !ok {
			return NoPadder
		}
	}
	return padder
}

func encodeMeta(reader hashReader, cd CipherData) (Envelope, error) {
	iv := base64.StdEncoding.EncodeToString(cd.IV)
	key := base64.StdEncoding.EncodeToString(cd.EncryptedKey)

	md5 := reader.GetValue()
	contentLength := reader.GetContentLength()

	md5Str := base64.StdEncoding.EncodeToString(md5)
	matdesc, err := cd.MaterialDescription.encodeDescription()
	if err != nil {
		return Envelope{}, err
	}

	return Envelope{
		CipherKey:             key,
		IV:                    iv,
		MatDesc:               string(matdesc),
		WrapAlg:               cd.WrapAlgorithm,
		CEKAlg:                cd.CEKAlgorithm,
		TagLen:                cd.TagLength,
		UnencryptedMD5:        md5Str,
		UnencryptedContentLen: strconv.FormatInt(contentLength, 10),
	}, nil
}
```

## `service/s3/s3crypto/cipher_util_test.go` (changed lines (17, 18, 31, 32, 45, 46, 59, 60, 70, 71, 84, 85, 110, 111, 142, 143, 144, 169, 170, 201, 202, 203, 228, 229, 258, 259, 260))

```go
package s3crypto

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/awstesting/unit"
	"github.com/aws/aws-sdk-go/service/kms"
)

func TestWrapFactory(t *testing.T) {
	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(unit.Session),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
		},
	}
	env := Envelope{
		WrapAlg: KMSWrap,
		MatDesc: `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)
	w, ok := wrap.(*kmsKeyHandler)

	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if wrap == nil {
		t.Error("expected non-nil value")
	}
	if !ok {
		t.Errorf("expected kmsKeyHandler, but received %v", *w)
	}
}
func TestWrapFactoryErrorNoWrap(t *testing.T) {
	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(unit.Session),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
		},
	}
	env := Envelope{
		WrapAlg: "none",
		MatDesc: `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)

	if err == nil {
		t.Error("expected error, but received none")
	}
	if wrap != nil {
		t.Errorf("expected nil wrap value, received %v", wrap)
	}
}

func TestWrapFactoryCustomEntry(t *testing.T) {
	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			"custom": (kmsKeyHandler{
				kms: kms.New(unit.Session),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
		},
	}
	env := Envelope{
		WrapAlg: "custom",
		MatDesc: `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)

	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if wrap == nil {
		t.Errorf("expected nil wrap value, received %v", wrap)
	}
}

func TestCEKFactory(t *testing.T) {
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	keyB64 := base64.URLEncoding.EncodeToString(key)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, fmt.Sprintf("%s%s%s", `{"KeyId":"test-key-id","Plaintext":"`, keyB64, `"}`))
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})

	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(sess),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
		},
		PadderRegistry: map[string]Padder{
			NoPadder.Name(): NoPadder,
		},
	}
	iv, err := hex.DecodeString("0d18e06c7c725ac9e362e1ce")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	ivB64 := base64.URLEncoding.EncodeToString(iv)

	cipherKey, err := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	cipherKeyB64 := base64.URLEncoding.EncodeToString(cipherKey)

	env := Envelope{
		WrapAlg:   KMSWrap,
		CEKAlg:    AESGCMNoPadding,
		CipherKey: cipherKeyB64,
		IV:        ivB64,
		MatDesc:   `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)
	cek, err := c.cekFromEnvelope(aws.BackgroundContext(), env, wrap)

	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if cek == nil {
		t.Errorf("expected non-nil cek")
	}
}

func TestCEKFactoryNoCEK(t *testing.T) {
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	keyB64 := base64.URLEncoding.EncodeToString(key)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, fmt.Sprintf("%s%s%s", `{"KeyId":"test-key-id","Plaintext":"`, keyB64, `"}`))
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})

	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(sess),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
		},
		PadderRegistry: map[string]Padder{
			NoPadder.Name(): NoPadder,
		},
	}
	iv, err := hex.DecodeString("0d18e06c7c725ac9e362e1ce")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	ivB64 := base64.URLEncoding.EncodeToString(iv)

	cipherKey, err := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	cipherKeyB64 := base64.URLEncoding.EncodeToString(cipherKey)

	env := Envelope{
		WrapAlg:   KMSWrap,
		CEKAlg:    "none",
		CipherKey: cipherKeyB64,
		IV:        ivB64,
		MatDesc:   `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)
	cek, err := c.cekFromEnvelope(aws.BackgroundContext(), env, wrap)

	if err == nil {
		t.Error("expected error, but received none")
	}
	if cek != nil {
		t.Errorf("expected nil cek value, received %v", wrap)
	}
}

func TestCEKFactoryCustomEntry(t *testing.T) {
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	keyB64 := base64.URLEncoding.EncodeToString(key)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, fmt.Sprintf("%s%s%s", `{"KeyId":"test-key-id","Plaintext":"`, keyB64, `"}`))
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})

	c := DecryptionClient{
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(sess),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			"custom": newAESGCMContentCipher,
		},
		PadderRegistry: map[string]Padder{},
	}
	iv, err := hex.DecodeString("0d18e06c7c725ac9e362e1ce")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	ivB64 := base64.URLEncoding.EncodeToString(iv)

	cipherKey, err := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	cipherKeyB64 := base64.URLEncoding.EncodeToString(cipherKey)

	env := Envelope{
		WrapAlg:   KMSWrap,
		CEKAlg:    "custom",
		CipherKey: cipherKeyB64,
		IV:        ivB64,
		MatDesc:   `{"kms_cmk_id":""}`,
	}
	wrap, err := c.wrapFromEnvelope(env)
	cek, err := c.cekFromEnvelope(aws.BackgroundContext(), env, wrap)

	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if cek == nil {
		t.Errorf("expected non-nil cek")
	}
}
```

## `service/s3/s3crypto/decryption_client.go` (changed lines (28, 48, 85, 86, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 123, 124, 125, 126, 135, 136, 137, 138, 139, 140))

```go
package s3crypto

import (
	"strings"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/client"
	"github.com/aws/aws-sdk-go/aws/request"
	"github.com/aws/aws-sdk-go/service/kms"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/aws/aws-sdk-go/service/s3/s3iface"
)

// WrapEntry is builder that return a proper key decrypter and error
type WrapEntry func(Envelope) (CipherDataDecrypter, error)

// CEKEntry is a builder thatn returns a proper content decrypter and error
type CEKEntry func(CipherData) (ContentCipher, error)

// DecryptionClient is an S3 crypto client. The decryption client
// will handle all get object requests from Amazon S3.
// Supported key wrapping algorithms:
//	*AWS KMS
//
// Supported content ciphers:
//	* AES/GCM
//	* AES/CBC
type DecryptionClient struct {
	S3Client s3iface.S3API
	// LoadStrategy is used to load the metadata either from the metadata of the object
	// or from a separate file in s3.
	//
	// Defaults to our default load strategy.
	LoadStrategy LoadStrategy

	WrapRegistry   map[string]WrapEntry
	CEKRegistry    map[string]CEKEntry
	PadderRegistry map[string]Padder
}

// NewDecryptionClient instantiates a new S3 crypto client
//
// Example:
//	sess := session.New()
//	svc := s3crypto.NewDecryptionClient(sess, func(svc *s3crypto.DecryptionClient{
//		// Custom client options here
//	}))
func NewDecryptionClient(prov client.ConfigProvider, options ...func(*DecryptionClient)) *DecryptionClient {
	s3client := s3.New(prov)

	s3client.Handlers.Build.PushBack(func(r *request.Request) {
		request.AddToUserAgent(r, "S3Crypto")
	})

	client := &DecryptionClient{
		S3Client: s3client,
		LoadStrategy: defaultV2LoadStrategy{
			client: s3client,
		},
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(prov),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
			strings.Join([]string{AESCBC, AESCBCPadder.Name()}, "/"): newAESCBCContentCipher,
		},
		PadderRegistry: map[string]Padder{
			strings.Join([]string{AESCBC, AESCBCPadder.Name()}, "/"): AESCBCPadder,
			"NoPadding": NoPadder,
		},
	}
	for _, option := range options {
		option(client)
	}

	return client
}

// GetObjectRequest will make a request to s3 and retrieve the object. In this process
// decryption will be done. The SDK only supports V2 reads of KMS and GCM.
//
// Example:
//	sess := session.New()
//	svc := s3crypto.NewDecryptionClient(sess)
//	req, out := svc.GetObjectRequest(&s3.GetObjectInput {
//	  Key: aws.String("testKey"),
//	  Bucket: aws.String("testBucket"),
//	})
//	err := req.Send()
func (c *DecryptionClient) GetObjectRequest(input *s3.GetObjectInput) (*request.Request, *s3.GetObjectOutput) {
	req, out := c.S3Client.GetObjectRequest(input)
	req.Handlers.Unmarshal.PushBack(func(r *request.Request) {
		env, err := c.LoadStrategy.Load(r)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}

		// If KMS should return the correct CEK algorithm with the proper
		// KMS key provider
		cipher, err := c.contentCipherFromEnvelope(r.Context(), env)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}

		reader, err := cipher.DecryptContents(out.Body)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}
		out.Body = reader
	})
	return req, out
}

// GetObject is a wrapper for GetObjectRequest
func (c *DecryptionClient) GetObject(input *s3.GetObjectInput) (*s3.GetObjectOutput, error) {
	req, out := c.GetObjectRequest(input)
	return out, req.Send()
}

// GetObjectWithContext is a wrapper for GetObjectRequest with the additional
// context, and request options support.
//
// GetObjectWithContext is the same as GetObject with the additional support for
// Context input parameters. The Context must not be nil. A nil Context will
// cause a panic. Use the Context to add deadlining, timeouts, etc. In the future
// this may create sub-contexts for individual underlying requests.
func (c *DecryptionClient) GetObjectWithContext(ctx aws.Context, input *s3.GetObjectInput, opts ...request.Option) (*s3.GetObjectOutput, error) {
	req, out := c.GetObjectRequest(input)
	req.SetContext(ctx)
	req.ApplyOptions(opts...)
	return out, req.Send()
}
```

## `service/s3/s3crypto/decryption_client.go` (changed lines (28, 48, 85, 86, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 123, 124, 125, 126, 135, 136, 137, 138, 139, 140))

```go
package s3crypto

import (
	"strings"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/client"
	"github.com/aws/aws-sdk-go/aws/request"
	"github.com/aws/aws-sdk-go/service/kms"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/aws/aws-sdk-go/service/s3/s3iface"
)

// WrapEntry is builder that return a proper key decrypter and error
type WrapEntry func(Envelope) (CipherDataDecrypter, error)

// CEKEntry is a builder thatn returns a proper content decrypter and error
type CEKEntry func(CipherData) (ContentCipher, error)

// DecryptionClient is an S3 crypto client. The decryption client
// will handle all get object requests from Amazon S3.
// Supported key wrapping algorithms:
//	*AWS KMS
//
// Supported content ciphers:
//	* AES/GCM
//	* AES/CBC
type DecryptionClient struct {
	S3Client s3iface.S3API
	// LoadStrategy is used to load the metadata either from the metadata of the object
	// or from a separate file in s3.
	//
	// Defaults to our default load strategy.
	LoadStrategy LoadStrategy

	WrapRegistry   map[string]WrapEntry
	CEKRegistry    map[string]CEKEntry
	PadderRegistry map[string]Padder
}

// NewDecryptionClient instantiates a new S3 crypto client
//
// Example:
//	sess := session.New()
//	svc := s3crypto.NewDecryptionClient(sess, func(svc *s3crypto.DecryptionClient{
//		// Custom client options here
//	}))
func NewDecryptionClient(prov client.ConfigProvider, options ...func(*DecryptionClient)) *DecryptionClient {
	s3client := s3.New(prov)

	s3client.Handlers.Build.PushBack(func(r *request.Request) {
		request.AddToUserAgent(r, "S3Crypto")
	})

	client := &DecryptionClient{
		S3Client: s3client,
		LoadStrategy: defaultV2LoadStrategy{
			client: s3client,
		},
		WrapRegistry: map[string]WrapEntry{
			KMSWrap: (kmsKeyHandler{
				kms: kms.New(prov),
			}).decryptHandler,
		},
		CEKRegistry: map[string]CEKEntry{
			AESGCMNoPadding: newAESGCMContentCipher,
			strings.Join([]string{AESCBC, AESCBCPadder.Name()}, "/"): newAESCBCContentCipher,
		},
		PadderRegistry: map[string]Padder{
			strings.Join([]string{AESCBC, AESCBCPadder.Name()}, "/"): AESCBCPadder,
			"NoPadding": NoPadder,
		},
	}
	for _, option := range options {
		option(client)
	}

	return client
}

// GetObjectRequest will make a request to s3 and retrieve the object. In this process
// decryption will be done. The SDK only supports V2 reads of KMS and GCM.
//
// Example:
//	sess := session.New()
//	svc := s3crypto.NewDecryptionClient(sess)
//	req, out := svc.GetObjectRequest(&s3.GetObjectInput {
//	  Key: aws.String("testKey"),
//	  Bucket: aws.String("testBucket"),
//	})
//	err := req.Send()
func (c *DecryptionClient) GetObjectRequest(input *s3.GetObjectInput) (*request.Request, *s3.GetObjectOutput) {
	req, out := c.S3Client.GetObjectRequest(input)
	req.Handlers.Unmarshal.PushBack(func(r *request.Request) {
		env, err := c.LoadStrategy.Load(r)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}

		// If KMS should return the correct CEK algorithm with the proper
		// KMS key provider
		cipher, err := c.contentCipherFromEnvelope(r.Context(), env)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}

		reader, err := cipher.DecryptContents(out.Body)
		if err != nil {
			r.Error = err
			out.Body.Close()
			return
		}
		out.Body = reader
	})
	return req, out
}

// GetObject is a wrapper for GetObjectRequest
func (c *DecryptionClient) GetObject(input *s3.GetObjectInput) (*s3.GetObjectOutput, error) {
	req, out := c.GetObjectRequest(input)
	return out, req.Send()
}

// GetObjectWithContext is a wrapper for GetObjectRequest with the additional
// context, and request options support.
//
// GetObjectWithContext is the same as GetObject with the additional support for
// Context input parameters. The Context must not be nil. A nil Context will
// cause a panic. Use the Context to add deadlining, timeouts, etc. In the future
// this may create sub-contexts for individual underlying requests.
func (c *DecryptionClient) GetObjectWithContext(ctx aws.Context, input *s3.GetObjectInput, opts ...request.Option) (*s3.GetObjectOutput, error) {
	req, out := c.GetObjectRequest(input)
	req.SetContext(ctx)
	req.ApplyOptions(opts...)
	return out, req.Send()
}
```

## `service/s3/s3crypto/doc.go` (changed lines (18, 19, 20, 21, 25, 26, 27, 30, 31, 32, 33, 34, 35, 36, 37, 39, 40, 41, 51, 52, 53, 54, 56, 57, 61, 62, 63, 64, 65))

```go
/*
Package s3crypto provides encryption to S3 using KMS and AES GCM.

Keyproviders are interfaces that handle masterkeys. Masterkeys are used to encrypt and decrypt the randomly
generated cipher keys. The SDK currently uses KMS to do this. A user does not need to provide a master key
since all that information is hidden in KMS.

Modes are interfaces that handle content encryption and decryption. It is an abstraction layer that instantiates
the ciphers. If content is being encrypted we generate the key and iv of the cipher. For decryption, we use the
metadata stored either on the object or an instruction file object to decrypt the contents.

Ciphers are interfaces that handle encryption and decryption of data. This may be key wrap ciphers or content
ciphers.

Creating an S3 cryptography client

	cmkID := "<some key ID>"
	sess := session.New()
	// Create the KeyProvider
	handler := s3crypto.NewKMSKeyGenerator(kms.New(sess), cmkID)

	// Create an encryption and decryption client
	// We need to pass the session here so S3 can use it. In addition, any decryption that
	// occurs will use the KMS client.
	svc := s3crypto.NewEncryptionClient(sess, s3crypto.AESGCMContentCipherBuilder(handler))
	svc := s3crypto.NewDecryptionClient(sess)

Configuration of the S3 cryptography client

	cfg := s3crypto.EncryptionConfig{
		// Save instruction files to separate objects
		SaveStrategy: NewS3SaveStrategy(session.New(), ""),
		// Change instruction file suffix to .example
		InstructionFileSuffix: ".example",
		// Set temp folder path
		TempFolderPath: "/path/to/tmp/folder/",
		// Any content less than the minimum file size will use memory
		// instead of writing the contents to a temp file.
		MinFileSize: int64(1024 * 1024 * 1024),
	}

The default SaveStrategy is to the object's header.

The InstructionFileSuffix defaults to .instruction. Careful here though, if you do this, be sure you know
what that suffix is in grabbing data.  All requests will look for fooKey.example instead of fooKey.instruction.
This suffix only affects gets and not puts. Put uses the keyprovider's suffix.

Registration of new wrap or cek algorithms are also supported by the SDK. Let's say we want to support `AES Wrap`
and `AES CTR`. Let's assume we have already defined the functionality.

	svc := s3crypto.NewDecryptionClient(sess)
	svc.WrapRegistry["AESWrap"] = NewAESWrap
	svc.CEKRegistry["AES/CTR/NoPadding"] = NewAESCTR

We have now registered these new algorithms to the decryption client. When the client calls `GetObject` and sees
the wrap as `AESWrap` then it'll use that wrap algorithm. This is also true for `AES/CTR/NoPadding`.

For encryption adding a custom content cipher builder and key handler will allow for encryption of custom
defined ciphers.

	// Our wrap algorithm, AESWrap
	handler := NewAESWrap(key, iv)
	// Our content cipher builder, AESCTRContentCipherBuilder
	svc := s3crypto.NewEncryptionClient(sess, NewAESCTRContentCipherBuilder(handler))
*/
package s3crypto
```

## `service/s3/s3crypto/encryption_client.go` (changed lines (4, 5, 6, 10, 23, 42, 43, 44, 45, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 144, 145, 146, 147, 156, 157, 158, 159, 160, 161))

```go
package s3crypto

import (
	"encoding/hex"
	"io"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/client"
	"github.com/aws/aws-sdk-go/aws/request"
	"github.com/aws/aws-sdk-go/internal/sdkio"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/aws/aws-sdk-go/service/s3/s3iface"
)

// DefaultMinFileSize is used to check whether we want to write to a temp file
// or store the data in memory.
const DefaultMinFileSize = 1024 * 512 * 5

// EncryptionClient is an S3 crypto client. By default the SDK will use Authentication mode which
// will use KMS for key wrapping and AES GCM for content encryption.
// AES GCM will load all data into memory. However, the rest of the content algorithms
// do not load the entire contents into memory.
type EncryptionClient struct {
	S3Client             s3iface.S3API
	ContentCipherBuilder ContentCipherBuilder
	// SaveStrategy will dictate where the envelope is saved.
	//
	// Defaults to the object's metadata
	SaveStrategy SaveStrategy
	// TempFolderPath is used to store temp files when calling PutObject.
	// Temporary files are needed to compute the X-Amz-Content-Sha256 header.
	TempFolderPath string
	// MinFileSize is the minimum size for the content to write to a
	// temporary file instead of using memory.
	MinFileSize int64
}

// NewEncryptionClient instantiates a new S3 crypto client
//
// Example:
//	cmkID := "arn:aws:kms:region:000000000000:key/00000000-0000-0000-0000-000000000000"
//	sess := session.New()
//	handler := s3crypto.NewKMSKeyGenerator(kms.New(sess), cmkID)
//	svc := s3crypto.New(sess, s3crypto.AESGCMContentCipherBuilder(handler))
func NewEncryptionClient(prov client.ConfigProvider, builder ContentCipherBuilder, options ...func(*EncryptionClient)) *EncryptionClient {
	s3client := s3.New(prov)

	s3client.Handlers.Build.PushBack(func(r *request.Request) {
		request.AddToUserAgent(r, "S3Crypto")
	})

	client := &EncryptionClient{
		S3Client:             s3client,
		ContentCipherBuilder: builder,
		SaveStrategy:         HeaderV2SaveStrategy{},
		MinFileSize:          DefaultMinFileSize,
	}

	for _, option := range options {
		option(client)
	}

	return client
}

// PutObjectRequest creates a temp file to encrypt the contents into. It then streams
// that data to S3.
//
// Example:
//	svc := s3crypto.New(session.New(), s3crypto.AESGCMContentCipherBuilder(handler))
//	req, out := svc.PutObjectRequest(&s3.PutObjectInput {
//	  Key: aws.String("testKey"),
//	  Bucket: aws.String("testBucket"),
//	  Body: strings.NewReader("test data"),
//	})
//	err := req.Send()
func (c *EncryptionClient) PutObjectRequest(input *s3.PutObjectInput) (*request.Request, *s3.PutObjectOutput) {
	req, out := c.S3Client.PutObjectRequest(input)

	// Get Size of file
	n, err := aws.SeekerLen(input.Body)
	if err != nil {
		req.Error = err
		return req, out
	}

	dst, err := getWriterStore(req, c.TempFolderPath, n >= c.MinFileSize)
	if err != nil {
		req.Error = err
		return req, out
	}

	req.Handlers.Build.PushFront(func(r *request.Request) {
		if err != nil {
			r.Error = err
			return
		}
		var encryptor ContentCipher
		if v, ok := c.ContentCipherBuilder.(ContentCipherBuilderWithContext); ok {
			encryptor, err = v.ContentCipherWithContext(r.Context())
		} else {
			encryptor, err = c.ContentCipherBuilder.ContentCipher()
		}
		if err != nil {
			r.Error = err
			return
		}

		md5 := newMD5Reader(input.Body)
		sha := newSHA256Writer(dst)
		reader, err := encryptor.EncryptContents(md5)
		if err != nil {
			r.Error = err
			return
		}

		_, err = io.Copy(sha, reader)
		if err != nil {
			r.Error = err
			return
		}

		data := encryptor.GetCipherData()
		env, err := encodeMeta(md5, data)
		if err != nil {
			r.Error = err
			return
		}

		shaHex := hex.EncodeToString(sha.GetValue())
		req.HTTPRequest.Header.Set("X-Amz-Content-Sha256", shaHex)

		dst.Seek(0, sdkio.SeekStart)
		input.Body = dst

		err = c.SaveStrategy.Save(env, r)
		r.Error = err
	})

	return req, out
}

// PutObject is a wrapper for PutObjectRequest
func (c *EncryptionClient) PutObject(input *s3.PutObjectInput) (*s3.PutObjectOutput, error) {
	req, out := c.PutObjectRequest(input)
	return out, req.Send()
}

// PutObjectWithContext is a wrapper for PutObjectRequest with the additional
// context, and request options support.
//
// PutObjectWithContext is the same as PutObject with the additional support for
// Context input parameters. The Context must not be nil. A nil Context will
// cause a panic. Use the Context to add deadlining, timeouts, etc. In the future
// this may create sub-contexts for individual underlying requests.
func (c *EncryptionClient) PutObjectWithContext(ctx aws.Context, input *s3.PutObjectInput, opts ...request.Option) (*s3.PutObjectOutput, error) {
	req, out := c.PutObjectRequest(input)
	req.SetContext(ctx)
	req.ApplyOptions(opts...)
	return out, req.Send()
}
```

## `service/s3/s3crypto/envelope.go` (changed lines (35, 36))

```go
package s3crypto

// DefaultInstructionKeySuffix is appended to the end of the instruction file key when
// grabbing or saving to S3
const DefaultInstructionKeySuffix = ".instruction"

const (
	metaHeader                     = "x-amz-meta"
	keyV1Header                    = "x-amz-key"
	keyV2Header                    = keyV1Header + "-v2"
	ivHeader                       = "x-amz-iv"
	matDescHeader                  = "x-amz-matdesc"
	cekAlgorithmHeader             = "x-amz-cek-alg"
	wrapAlgorithmHeader            = "x-amz-wrap-alg"
	tagLengthHeader                = "x-amz-tag-len"
	unencryptedMD5Header           = "x-amz-unencrypted-content-md5"
	unencryptedContentLengthHeader = "x-amz-unencrypted-content-length"
)

// Envelope encryption starts off by generating a random symmetric key using
// AES GCM. The SDK generates a random IV based off the encryption cipher
// chosen. The master key that was provided, whether by the user or KMS, will be used
// to encrypt the randomly generated symmetric key and base64 encode the iv. This will
// allow for decryption of that same data later.
type Envelope struct {
	// IV is the randomly generated IV base64 encoded.
	IV string `json:"x-amz-iv"`
	// CipherKey is the randomly generated cipher key.
	CipherKey string `json:"x-amz-key-v2"`
	// MaterialDesc is a description to distinguish from other envelopes.
	MatDesc               string `json:"x-amz-matdesc"`
	WrapAlg               string `json:"x-amz-wrap-alg"`
	CEKAlg                string `json:"x-amz-cek-alg"`
	TagLen                string `json:"x-amz-tag-len"`
	UnencryptedMD5        string `json:"x-amz-unencrypted-content-md5"`
	UnencryptedContentLen string `json:"x-amz-unencrypted-content-length"`
}
```

## `service/s3/s3crypto/key_handler.go` (changed lines (23, 33, 34, 35, 36, 37))

```go
package s3crypto

import (
	"crypto/rand"

	"github.com/aws/aws-sdk-go/aws"
)

// CipherDataGenerator handles generating proper key and IVs of proper size for the
// content cipher. CipherDataGenerator will also encrypt the key and store it in
// the CipherData.
type CipherDataGenerator interface {
	GenerateCipherData(int, int) (CipherData, error)
}

// CipherDataGeneratorWithContext handles generating proper key and IVs of
// proper size for the content cipher. CipherDataGenerator will also encrypt
// the key and store it in the CipherData.
type CipherDataGeneratorWithContext interface {
	GenerateCipherDataWithContext(aws.Context, int, int) (CipherData, error)
}

// CipherDataDecrypter is a handler to decrypt keys from the envelope.
type CipherDataDecrypter interface {
	DecryptKey([]byte) ([]byte, error)
}

// CipherDataDecrypterWithContext is a handler to decrypt keys from the envelope with request context.
type CipherDataDecrypterWithContext interface {
	DecryptKeyWithContext(aws.Context, []byte) ([]byte, error)
}

func generateBytes(n int) []byte {
	b := make([]byte, n)
	rand.Read(b)
	return b
}
```

## `service/s3/s3crypto/key_handler_test.go` (changed lines (8, 9, 12, 13, 16, 17))

```go
package s3crypto

import (
	"testing"
)

func TestGenerateBytes(t *testing.T) {
	b := generateBytes(5)
	if e, a := 5, len(b); e != a {
		t.Errorf("expected %d, but received %d", e, a)
	}
	b = generateBytes(0)
	if e, a := 0, len(b); e != a {
		t.Errorf("expected %d, but received %d", e, a)
	}
	b = generateBytes(1024)
	if e, a := 1024, len(b); e != a {
		t.Errorf("expected %d, but received %d", e, a)
	}
}
```

## `service/s3/s3crypto/kms_key_handler.go` (changed lines (4, 13, 17, 18, 19, 31, 35, 36, 42, 43, 44, 47, 50, 51, 52, 53, 54, 55, 60, 70, 79, 89, 90, 96, 124, 127, 129, 130, 137, 138, 141, 142, 143, 147))

```go
package s3crypto

import (
	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/awserr"
	"github.com/aws/aws-sdk-go/service/kms"
	"github.com/aws/aws-sdk-go/service/kms/kmsiface"
)

const (
	// KMSWrap is a constant used during decryption to build a KMS key handler.
	KMSWrap = "kms"
)

// kmsKeyHandler will make calls to KMS to get the masterkey
type kmsKeyHandler struct {
	kms   kmsiface.KMSAPI
	cmkID *string

	CipherData
}

// NewKMSKeyGenerator builds a new KMS key provider using the customer key ID and material
// description.
//
// Example:
//	sess := session.New(&aws.Config{})
//	cmkID := "arn to key"
//	matdesc := s3crypto.MaterialDescription{}
//	handler := s3crypto.NewKMSKeyGenerator(kms.New(sess), cmkID)
func NewKMSKeyGenerator(kmsClient kmsiface.KMSAPI, cmkID string) CipherDataGenerator {
	return NewKMSKeyGeneratorWithMatDesc(kmsClient, cmkID, MaterialDescription{})
}

// NewKMSKeyGeneratorWithMatDesc builds a new KMS key provider using the customer key ID and material
// description.
//
// Example:
//	sess := session.New(&aws.Config{})
//	cmkID := "arn to key"
//	matdesc := s3crypto.MaterialDescription{}
//	handler := s3crypto.NewKMSKeyGeneratorWithMatDesc(kms.New(sess), cmkID, matdesc)
func NewKMSKeyGeneratorWithMatDesc(kmsClient kmsiface.KMSAPI, cmkID string, matdesc MaterialDescription) CipherDataGenerator {
	if matdesc == nil {
		matdesc = MaterialDescription{}
	}
	matdesc["kms_cmk_id"] = &cmkID

	// These values are read only making them thread safe
	kp := &kmsKeyHandler{
		kms:   kmsClient,
		cmkID: &cmkID,
	}
	// These values are read only making them thread safe
	kp.CipherData.WrapAlgorithm = KMSWrap
	kp.CipherData.MaterialDescription = matdesc
	return kp
}

// NewKMSWrapEntry builds returns a new KMS key provider and its decrypt handler.
//
// Example:
//	sess := session.New(&aws.Config{})
//	customKMSClient := kms.New(sess)
//	decryptHandler := s3crypto.NewKMSWrapEntry(customKMSClient)
//
//	svc := s3crypto.NewDecryptionClient(sess, func(svc *s3crypto.DecryptionClient) {
//		svc.WrapRegistry[s3crypto.KMSWrap] = decryptHandler
//	}))
func NewKMSWrapEntry(kmsClient kmsiface.KMSAPI) WrapEntry {
	// These values are read only making them thread safe
	kp := &kmsKeyHandler{
		kms: kmsClient,
	}

	return kp.decryptHandler
}

// decryptHandler initializes a KMS keyprovider with a material description. This
// is used with Decrypting kms content, due to the cmkID being in the material description.
func (kp kmsKeyHandler) decryptHandler(env Envelope) (CipherDataDecrypter, error) {
	m := MaterialDescription{}
	err := m.decodeDescription([]byte(env.MatDesc))
	if err != nil {
		return nil, err
	}

	cmkID, ok := m["kms_cmk_id"]
	if !ok {
		return nil, awserr.New("MissingCMKIDError", "Material description is missing CMK ID", nil)
	}

	kp.CipherData.MaterialDescription = m
	kp.cmkID = cmkID
	kp.WrapAlgorithm = KMSWrap
	return &kp, nil
}

// DecryptKey makes a call to KMS to decrypt the key.
func (kp *kmsKeyHandler) DecryptKey(key []byte) ([]byte, error) {
	return kp.DecryptKeyWithContext(aws.BackgroundContext(), key)
}

// DecryptKeyWithContext makes a call to KMS to decrypt the key with request context.
func (kp *kmsKeyHandler) DecryptKeyWithContext(ctx aws.Context, key []byte) ([]byte, error) {
	out, err := kp.kms.DecryptWithContext(ctx,
		&kms.DecryptInput{
			EncryptionContext: kp.CipherData.MaterialDescription,
			CiphertextBlob:    key,
			GrantTokens:       []*string{},
		})
	if err != nil {
		return nil, err
	}
	return out.Plaintext, nil
}

// GenerateCipherData makes a call to KMS to generate a data key, Upon making
// the call, it also sets the encrypted key.
func (kp *kmsKeyHandler) GenerateCipherData(keySize, ivSize int) (CipherData, error) {
	return kp.GenerateCipherDataWithContext(aws.BackgroundContext(), keySize, ivSize)
}

// GenerateCipherDataWithContext makes a call to KMS to generate a data key,
// Upon making the call, it also sets the encrypted key.
func (kp *kmsKeyHandler) GenerateCipherDataWithContext(ctx aws.Context, keySize, ivSize int) (CipherData, error) {
	out, err := kp.kms.GenerateDataKeyWithContext(ctx,
		&kms.GenerateDataKeyInput{
			EncryptionContext: kp.CipherData.MaterialDescription,
			KeyId:             kp.cmkID,
			KeySpec:           aws.String("AES_256"),
		})
	if err != nil {
		return CipherData{}, err
	}

	iv := generateBytes(ivSize)
	cd := CipherData{
		Key:                 out.Plaintext,
		IV:                  iv,
		WrapAlgorithm:       KMSWrap,
		MaterialDescription: kp.CipherData.MaterialDescription,
		EncryptedKey:        out.CiphertextBlob,
	}
	return cd, nil
}
```

## `service/s3/s3crypto/kms_key_handler_test.go` (changed lines (7, 8, 108, 109, 112, 123, 124, 125, 126, 127))

```go
package s3crypto

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/awstesting/unit"
	"github.com/aws/aws-sdk-go/service/kms"
)

func TestBuildKMSEncryptHandler(t *testing.T) {
	svc := kms.New(unit.Session)
	handler := NewKMSKeyGenerator(svc, "testid")
	if handler == nil {
		t.Error("expected non-nil handler")
	}
}

func TestBuildKMSEncryptHandlerWithMatDesc(t *testing.T) {
	svc := kms.New(unit.Session)
	handler := NewKMSKeyGeneratorWithMatDesc(svc, "testid", MaterialDescription{
		"Testing": aws.String("123"),
	})
	if handler == nil {
		t.Error("expected non-nil handler")
	}

	kmsHandler := handler.(*kmsKeyHandler)
	expected := MaterialDescription{
		"kms_cmk_id": aws.String("testid"),
		"Testing":    aws.String("123"),
	}

	if !reflect.DeepEqual(expected, kmsHandler.CipherData.MaterialDescription) {
		t.Errorf("expected %v, but received %v", expected, kmsHandler.CipherData.MaterialDescription)
	}
}

func TestKMSGenerateCipherData(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, `{"CiphertextBlob":"AQEDAHhqBCCY1MSimw8gOGcUma79cn4ANvTtQyv9iuBdbcEF1QAAAH4wfAYJKoZIhvcNAQcGoG8wbQIBADBoBgkqhkiG9w0BBwEwHgYJYIZIAWUDBAEuMBEEDJ6IcN5E4wVbk38MNAIBEIA7oF1E3lS7FY9DkoxPc/UmJsEwHzL82zMqoLwXIvi8LQHr8If4Lv6zKqY8u0+JRgSVoqCvZDx3p8Cn6nM=","KeyId":"arn:aws:kms:us-west-2:042062605278:key/c80a5cdb-8d09-4f9f-89ee-df01b2e3870a","Plaintext":"6tmyz9JLBE2yIuU7iXpArqpDVle172WSmxjcO6GNT7E="}`)
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})

	svc := kms.New(sess)
	handler := NewKMSKeyGenerator(svc, "testid")

	keySize := 32
	ivSize := 16

	cd, err := handler.GenerateCipherData(keySize, ivSize)
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}
	if keySize != len(cd.Key) {
		t.Errorf("expected %d, but received %d", keySize, len(cd.Key))
	}
	if ivSize != len(cd.IV) {
		t.Errorf("expected %d, but received %d", ivSize, len(cd.IV))
	}
}

func TestKMSDecrypt(t *testing.T) {
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	keyB64 := base64.URLEncoding.EncodeToString(key)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, fmt.Sprintf("%s%s%s", `{"KeyId":"test-key-id","Plaintext":"`, keyB64, `"}`))
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})
	handler, err := (kmsKeyHandler{kms: kms.New(sess)}).decryptHandler(Envelope{MatDesc: `{"kms_cmk_id":"test"}`})
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}

	plaintextKey, err := handler.DecryptKey([]byte{1, 2, 3, 4})
	if err != nil {
		t.Errorf("expected no error, but received %v", err)
	}

	if !bytes.Equal(key, plaintextKey) {
		t.Errorf("expected %v, but received %v", key, plaintextKey)
	}
}

func TestKMSDecryptBadJSON(t *testing.T) {
	key, _ := hex.DecodeString("31bdadd96698c204aa9ce1448ea94ae1fb4a9a0b3c9d773b51bb1822666b8f22")
	keyB64 := base64.URLEncoding.EncodeToString(key)
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, fmt.Sprintf("%s%s%s", `{"KeyId":"test-key-id","Plaintext":"`, keyB64, `"}`))
	}))
	defer ts.Close()

	sess := unit.Session.Copy(&aws.Config{
		MaxRetries:       aws.Int(0),
		Endpoint:         aws.String(ts.URL),
		DisableSSL:       aws.Bool(true),
		S3ForcePathStyle: aws.Bool(true),
		Region:           aws.String("us-west-2"),
	})

	_, err := (kmsKeyHandler{kms: kms.New(sess)}).decryptHandler(Envelope{MatDesc: `{"kms_cmk_id":"test"`})
	if err == nil {
		t.Errorf("expected error, but received none")
	}
}
```

## `service/s3/s3crypto/mock_test.go` (changed lines (11, 12, 13, 30))

```go
package s3crypto_test

import (
	"bytes"
	"io"
	"io/ioutil"

	"github.com/aws/aws-sdk-go/service/s3/s3crypto"
)

type mockGenerator struct {
}

func (m mockGenerator) GenerateCipherData(keySize, ivSize int) (s3crypto.CipherData, error) {
	cd := s3crypto.CipherData{
		Key: make([]byte, keySize),
		IV:  make([]byte, ivSize),
	}
	return cd, nil
}

func (m mockGenerator) EncryptKey(key []byte) ([]byte, error) {
	size := len(key)
	b := bytes.Repeat([]byte{1}, size)
	return b, nil
}

func (m mockGenerator) DecryptKey(key []byte) ([]byte, error) {
	return make([]byte, 16), nil

}

type mockCipherBuilder struct {
	generator s3crypto.CipherDataGenerator
}

func (builder mockCipherBuilder) ContentCipher() (s3crypto.ContentCipher, error) {
	cd, err := builder.generator.GenerateCipherData(32, 16)
	if err != nil {
		return nil, err
	}
	return &mockContentCipher{cd}, nil
}

type mockContentCipher struct {
	cd s3crypto.CipherData
}

func (cipher *mockContentCipher) GetCipherData() s3crypto.CipherData {
	return cipher.cd
}

func (cipher *mockContentCipher) EncryptContents(src io.Reader) (io.Reader, error) {
	b, err := ioutil.ReadAll(src)
	if err != nil {
		return nil, err
	}
	size := len(b)
	b = bytes.Repeat([]byte{1}, size)
	return bytes.NewReader(b), nil
}

func (cipher *mockContentCipher) DecryptContents(src io.ReadCloser) (io.ReadCloser, error) {
	b, err := ioutil.ReadAll(src)
	if err != nil {
		return nil, err
	}
	size := len(b)
	return ioutil.NopCloser(bytes.NewReader(make([]byte, size))), nil
}
```

## `service/s3/s3crypto/strategy.go` (changed lines (66,))

```go
package s3crypto

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"net/http"
	"strings"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/awserr"
	"github.com/aws/aws-sdk-go/aws/request"
	"github.com/aws/aws-sdk-go/service/s3"
)

// SaveStrategy is how the data's metadata wants to be saved
type SaveStrategy interface {
	Save(Envelope, *request.Request) error
}

// S3SaveStrategy will save the metadata to a separate instruction file in S3
type S3SaveStrategy struct {
	Client                *s3.S3
	InstructionFileSuffix string
}

// Save will save the envelope contents to s3.
func (strat S3SaveStrategy) Save(env Envelope, req *request.Request) error {
	input := req.Params.(*s3.PutObjectInput)
	b, err := json.Marshal(env)
	if err != nil {
		return err
	}

	instInput := s3.PutObjectInput{
		Bucket: input.Bucket,
		Body:   bytes.NewReader(b),
	}

	if strat.InstructionFileSuffix == "" {
		instInput.Key = aws.String(*input.Key + DefaultInstructionKeySuffix)
	} else {
		instInput.Key = aws.String(*input.Key + strat.InstructionFileSuffix)
	}

	_, err = strat.Client.PutObject(&instInput)
	return err
}

// HeaderV2SaveStrategy will save the metadata of the crypto contents to the header of
// the object.
type HeaderV2SaveStrategy struct{}

// Save will save the envelope to the request's header.
func (strat HeaderV2SaveStrategy) Save(env Envelope, req *request.Request) error {
	input := req.Params.(*s3.PutObjectInput)
	if input.Metadata == nil {
		input.Metadata = map[string]*string{}
	}

	input.Metadata[http.CanonicalHeaderKey(keyV2Header)] = &env.CipherKey
	input.Metadata[http.CanonicalHeaderKey(ivHeader)] = &env.IV
	input.Metadata[http.CanonicalHeaderKey(matDescHeader)] = &env.MatDesc
	input.Metadata[http.CanonicalHeaderKey(wrapAlgorithmHeader)] = &env.WrapAlg
	input.Metadata[http.CanonicalHeaderKey(cekAlgorithmHeader)] = &env.CEKAlg
	input.Metadata[http.CanonicalHeaderKey(unencryptedMD5Header)] = &env.UnencryptedMD5
	input.Metadata[http.CanonicalHeaderKey(unencryptedContentLengthHeader)] = &env.UnencryptedContentLen

	if len(env.TagLen) > 0 {
		input.Metadata[http.CanonicalHeaderKey(tagLengthHeader)] = &env.TagLen
	}
	return nil
}

// LoadStrategy ...
type LoadStrategy interface {
	Load(*request.Request) (Envelope, error)
}

// S3LoadStrategy will load the instruction file from s3
type S3LoadStrategy struct {
	Client                *s3.S3
	InstructionFileSuffix string
}

// Load from a given instruction file suffix
func (load S3LoadStrategy) Load(req *request.Request) (Envelope, error) {
	env := Envelope{}
	if load.InstructionFileSuffix == "" {
		load.InstructionFileSuffix = DefaultInstructionKeySuffix
	}

	input := req.Params.(*s3.GetObjectInput)
	out, err := load.Client.GetObject(&s3.GetObjectInput{
		Key:    aws.String(strings.Join([]string{*input.Key, load.InstructionFileSuffix}, "")),
		Bucket: input.Bucket,
	})
	if err != nil {
		return env, err
	}

	b, err := ioutil.ReadAll(out.Body)
	if err != nil {
		return env, err
	}
	err = json.Unmarshal(b, &env)
	return env, err
}

// HeaderV2LoadStrategy will load the envelope from the metadata
type HeaderV2LoadStrategy struct{}

// Load from a given object's header
func (load HeaderV2LoadStrategy) Load(req *request.Request) (Envelope, error) {
	env := Envelope{}
	env.CipherKey = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, keyV2Header}, "-"))
	env.IV = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, ivHeader}, "-"))
	env.MatDesc = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, matDescHeader}, "-"))
	env.WrapAlg = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, wrapAlgorithmHeader}, "-"))
	env.CEKAlg = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, cekAlgorithmHeader}, "-"))
	env.TagLen = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, tagLengthHeader}, "-"))
	env.UnencryptedMD5 = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, unencryptedMD5Header}, "-"))
	env.UnencryptedContentLen = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, unencryptedContentLengthHeader}, "-"))
	return env, nil
}

type defaultV2LoadStrategy struct {
	client *s3.S3
	suffix string
}

func (load defaultV2LoadStrategy) Load(req *request.Request) (Envelope, error) {
	if value := req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, keyV2Header}, "-")); value != "" {
		strat := HeaderV2LoadStrategy{}
		return strat.Load(req)
	} else if value = req.HTTPResponse.Header.Get(strings.Join([]string{metaHeader, keyV1Header}, "-")); value != "" {
		return Envelope{}, awserr.New("V1NotSupportedError", "The AWS SDK for Go does not support version 1", nil)
	}

	strat := S3LoadStrategy{
		Client:                load.client,
		InstructionFileSuffix: load.suffix,
	}
	return strat.Load(req)
}
```

## `service/s3/s3crypto/strategy_test.go` (changed lines (4, 9, 36, 56, 78))

```go
package s3crypto_test

import (
	"reflect"
	"testing"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/request"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/aws/aws-sdk-go/service/s3/s3crypto"
)

func TestHeaderV2SaveStrategy(t *testing.T) {
	cases := []struct {
		env      s3crypto.Envelope
		expected map[string]*string
	}{
		{
			s3crypto.Envelope{
				CipherKey:             "Foo",
				IV:                    "Bar",
				MatDesc:               "{}",
				WrapAlg:               s3crypto.KMSWrap,
				CEKAlg:                s3crypto.AESGCMNoPadding,
				TagLen:                "128",
				UnencryptedMD5:        "hello",
				UnencryptedContentLen: "0",
			},
			map[string]*string{
				"X-Amz-Key-V2":                     aws.String("Foo"),
				"X-Amz-Iv":                         aws.String("Bar"),
				"X-Amz-Matdesc":                    aws.String("{}"),
				"X-Amz-Wrap-Alg":                   aws.String(s3crypto.KMSWrap),
				"X-Amz-Cek-Alg":                    aws.String(s3crypto.AESGCMNoPadding),
				"X-Amz-Tag-Len":                    aws.String("128"),
				"X-Amz-Unencrypted-Content-Md5":    aws.String("hello"),
				"X-Amz-Unencrypted-Content-Length": aws.String("0"),
			},
		},
		{
			s3crypto.Envelope{
				CipherKey:             "Foo",
				IV:                    "Bar",
				MatDesc:               "{}",
				WrapAlg:               s3crypto.KMSWrap,
				CEKAlg:                s3crypto.AESGCMNoPadding,
				UnencryptedMD5:        "hello",
				UnencryptedContentLen: "0",
			},
			map[string]*string{
				"X-Amz-Key-V2":                     aws.String("Foo"),
				"X-Amz-Iv":                         aws.String("Bar"),
				"X-Amz-Matdesc":                    aws.String("{}"),
				"X-Amz-Wrap-Alg":                   aws.String(s3crypto.KMSWrap),
				"X-Amz-Cek-Alg":                    aws.String(s3crypto.AESGCMNoPadding),
				"X-Amz-Unencrypted-Content-Md5":    aws.String("hello"),
				"X-Amz-Unencrypted-Content-Length": aws.String("0"),
			},
		},
	}

	for _, c := range cases {
		params := &s3.PutObjectInput{}
		req := &request.Request{
			Params: params,
		}
		strat := s3crypto.HeaderV2SaveStrategy{}
		err := strat.Save(c.env, req)
		if err != nil {
			t.Errorf("expected no error, but received %v", err)
		}

		if !reflect.DeepEqual(c.expected, params.Metadata) {
			t.Errorf("expected %v, but received %v", c.expected, params.Metadata)
		}
	}
}
```
