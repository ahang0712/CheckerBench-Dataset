# Patch-overlapping Java context before the fix

## `src/main/java/com/southernstorm/noise/protocol/AESGCMFallbackCipherState.java` (changed lines (188, 189, 190, 191, 192, 217, 218, 219, 223, 224, 225, 226, 227))

```java
/*
 * Copyright (C) 2016 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package com.southernstorm.noise.protocol;

import java.util.Arrays;

import javax.crypto.BadPaddingException;
import javax.crypto.ShortBufferException;

import com.southernstorm.noise.crypto.GHASH;
import com.southernstorm.noise.crypto.RijndaelAES;

/**
 * Fallback implementation of "AESGCM" on platforms where
 * the JCA/JCE does not have a suitable GCM or CTR provider.
 */
class AESGCMFallbackCipherState implements CipherState {

	private RijndaelAES aes;
	private long n;
	private byte[] iv;
	private byte[] enciv;
	private byte[] hashKey;
	private GHASH ghash;
	private boolean haskey;

	/**
	 * Constructs a new cipher state for the "AESGCM" algorithm.
	 */
	public AESGCMFallbackCipherState()
	{
		aes = new RijndaelAES();
		n = 0;
		iv = new byte [16];
		enciv = new byte [16];
		hashKey = new byte [16];
		ghash = new GHASH();
		haskey = false;
	}

	@Override
	public void destroy() {
		aes.destroy();
		ghash.destroy();
		Noise.destroy(hashKey);
		Noise.destroy(iv);
		Noise.destroy(enciv);
	}

	@Override
	public String getCipherName() {
		return "AESGCM";
	}

	@Override
	public int getKeyLength() {
		return 32;
	}

	@Override
	public int getMACLength() {
		return haskey ? 16 : 0;
	}

	@Override
	public void initializeKey(byte[] key, int offset) {
		// Set up the AES key.
		aes.setupEnc(key, offset, 256);
		haskey = true;

		// Generate the hashing key by encrypting a block of zeroes.
		Arrays.fill(hashKey, (byte)0);
		aes.encrypt(hashKey, 0, hashKey, 0);
		ghash.reset(hashKey, 0);
		
		// Reset the nonce.
		n = 0;
	}

	@Override
	public boolean hasKey() {
		return haskey;
	}
	
	/**
	 * Set up to encrypt or decrypt the next packet.
	 * 
	 * @param ad The associated data for the packet.
	 */
	private void setup(byte[] ad)
	{
		// Check for nonce wrap-around.
		if (n == -1L)
			throw new IllegalStateException("Nonce has wrapped around");
		
		// Format the counter/IV block.
		iv[0] = 0;
		iv[1] = 0;
		iv[2] = 0;
		iv[3] = 0;
		iv[4] = (byte)(n >> 56);
		iv[5] = (byte)(n >> 48);
		iv[6] = (byte)(n >> 40);
		iv[7] = (byte)(n >> 32);
		iv[8] = (byte)(n >> 24);
		iv[9] = (byte)(n >> 16);
		iv[10] = (byte)(n >> 8);
		iv[11] = (byte)n;
		iv[12] = 0;
		iv[13] = 0;
		iv[14] = 0;
		iv[15] = 1;
		++n;
		
		// Encrypt a block of zeroes to generate the hash key to XOR
		// the GHASH tag with at the end of the encrypt/decrypt operation.
		Arrays.fill(hashKey, (byte)0);
		aes.encrypt(iv, 0, hashKey, 0);
		
		// Initialize the GHASH with the associated data value.
		ghash.reset();
		if (ad != null) {
			ghash.update(ad, 0, ad.length);
			ghash.pad();
		}
	}

	/**
	 * Encrypts a block in CTR mode.
	 * 
	 * @param plaintext The plaintext to encrypt.
	 * @param plaintextOffset Offset of the first plaintext byte.
	 * @param ciphertext The resulting ciphertext.
	 * @param ciphertextOffset Offset of the first ciphertext byte.
	 * @param length The number of bytes to encrypt.
	 * 
	 * This function can also be used to decrypt.
	 */
	private void encryptCTR(byte[] plaintext, int plaintextOffset, byte[] ciphertext, int ciphertextOffset, int length)
	{
		while (length > 0) {
			// Increment the IV and encrypt it to get the next keystream block.
			if (++(iv[15]) == 0)
				if (++(iv[14]) == 0)
					if (++(iv[13]) == 0)
						++(iv[12]);
			aes.encrypt(iv, 0, enciv, 0);
			
			// XOR the keystream block with the plaintext to create the ciphertext.
			int temp = length;
			if (temp > 16)
				temp = 16;
			for (int index = 0; index < temp; ++index)
				ciphertext[ciphertextOffset + index] = (byte)(plaintext[plaintextOffset + index] ^ enciv[index]);
			
			// Advance to the next block.
			plaintextOffset += temp;
			ciphertextOffset += temp;
			length -= temp;
		}
	}

	@Override
	public int encryptWithAd(byte[] ad, byte[] plaintext, int plaintextOffset,
			byte[] ciphertext, int ciphertextOffset, int length)
			throws ShortBufferException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (!haskey) {
			// The key is not set yet - return the plaintext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(plaintext, plaintextOffset, ciphertext, ciphertextOffset, length);
			return length;
		}
		if (space < 16 || length > (space - 16))
			throw new ShortBufferException();
		setup(ad);
		encryptCTR(plaintext, plaintextOffset, ciphertext, ciphertextOffset, length);
		ghash.update(ciphertext, ciphertextOffset, length);
		ghash.pad(ad != null ? ad.length : 0, length);
		ghash.finish(ciphertext, ciphertextOffset + length, 16);
		for (int index = 0; index < 16; ++index)
			ciphertext[ciphertextOffset + length + index] ^= hashKey[index];
		return length + 16;
	}

	@Override
	public int decryptWithAd(byte[] ad, byte[] ciphertext,
			int ciphertextOffset, byte[] plaintext, int plaintextOffset,
			int length) throws ShortBufferException, BadPaddingException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (length > space)
			throw new ShortBufferException();
		if (plaintextOffset > plaintext.length)
			space = 0;
		else
			space = plaintext.length - plaintextOffset;
		if (!haskey) {
			// The key is not set yet - return the ciphertext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(ciphertext, ciphertextOffset, plaintext, plaintextOffset, length);
			return length;
		}
		if (length < 16)
			Noise.throwBadTagException();
		int dataLen = length - 16;
		if (dataLen > space)
			throw new ShortBufferException();
		setup(ad);
		ghash.update(ciphertext, ciphertextOffset, dataLen);
		ghash.pad(ad != null ? ad.length : 0, dataLen);
		ghash.finish(enciv, 0, 16);
		int temp = 0;
		for (int index = 0; index < 16; ++index)
			temp |= (hashKey[index] ^ enciv[index] ^ ciphertext[ciphertextOffset + dataLen + index]);
		if ((temp & 0xFF) != 0)
			Noise.throwBadTagException();
		encryptCTR(ciphertext, ciphertextOffset, plaintext, plaintextOffset, dataLen);
		return dataLen;
	}

	@Override
	public CipherState fork(byte[] key, int offset) {
		CipherState cipher;
		cipher = new AESGCMFallbackCipherState();
		cipher.initializeKey(key, offset);
		return cipher;
	}

	@Override
	public void setNonce(long nonce) {
		n = nonce;
	}
}
```

## `src/main/java/com/southernstorm/noise/protocol/AESGCMOnCtrCipherState.java` (changed lines (221, 222, 223, 224, 225, 265, 266, 267, 271, 272, 273, 274, 275))

```java
/*
 * Copyright (C) 2016 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package com.southernstorm.noise.protocol;

import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import javax.crypto.ShortBufferException;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

import com.southernstorm.noise.crypto.GHASH;

/**
 * Emulates the "AESGCM" cipher for Noise using the "AES/CTR/NoPadding"
 * transformation from JCA/JCE.
 * 
 * This class is used on platforms that don't have "AES/GCM/NoPadding",
 * but which do have the older "AES/CTR/NoPadding".
 */
class AESGCMOnCtrCipherState implements CipherState {

	private Cipher cipher;
	private SecretKeySpec keySpec;
	private long n;
	private byte[] iv;
	private byte[] hashKey;
	private GHASH ghash;

	/**
	 * Constructs a new cipher state for the "AESGCM" algorithm.
	 * 
	 * @throws NoSuchAlgorithmException The system does not have a
	 * provider for this algorithm.
	 */
	public AESGCMOnCtrCipherState() throws NoSuchAlgorithmException
	{
		try {
			cipher = Cipher.getInstance("AES/CTR/NoPadding");
		} catch (NoSuchPaddingException e) {
			// AES/CTR is available, but not the unpadded version?  Huh?
			throw new NoSuchAlgorithmException("AES/CTR/NoPadding not available", e);
		}
		keySpec = null;
		n = 0;
		iv = new byte [16];
		hashKey = new byte [16];
		ghash = new GHASH();
		
		// Try to set a 256-bit key on the cipher.  Some JCE's are
		// configured to disallow 256-bit AES if an extra policy
		// file has not been installed.
		try {
			SecretKeySpec spec = new SecretKeySpec(new byte [32], "AES");
			IvParameterSpec params = new IvParameterSpec(iv);
			cipher.init(Cipher.ENCRYPT_MODE, spec, params);
		} catch (InvalidKeyException e) {
			throw new NoSuchAlgorithmException("AES/CTR/NoPadding does not support 256-bit keys", e);
		} catch (InvalidAlgorithmParameterException e) {
			throw new NoSuchAlgorithmException("AES/CTR/NoPadding does not support 256-bit keys", e);
		}
	}

	@Override
	public void destroy() {
		// There doesn't seem to be a standard API to clean out a Cipher.
		// So we instead set the key and IV to all-zeroes to hopefully
		// destroy the sensitive data in the cipher instance.
		ghash.destroy();
		Noise.destroy(hashKey);
		Noise.destroy(iv);
		keySpec = new SecretKeySpec(new byte [32], "AES");
		IvParameterSpec params = new IvParameterSpec(iv);
		try {
			cipher.init(Cipher.ENCRYPT_MODE, keySpec, params);
		} catch (InvalidKeyException e) {
			// Shouldn't happen.
		} catch (InvalidAlgorithmParameterException e) {
			// Shouldn't happen.
		}
	}

	@Override
	public String getCipherName() {
		return "AESGCM";
	}

	@Override
	public int getKeyLength() {
		return 32;
	}

	@Override
	public int getMACLength() {
		return keySpec != null ? 16 : 0;
	}

	@Override
	public void initializeKey(byte[] key, int offset) {
		// Set the encryption key.
		keySpec = new SecretKeySpec(key, offset, 32, "AES");
		
		// Generate the hashing key by encrypting a block of zeroes.
		Arrays.fill(iv, (byte)0);
		Arrays.fill(hashKey, (byte)0);
		try {
			cipher.init(Cipher.ENCRYPT_MODE, keySpec, new IvParameterSpec(iv));
		} catch (InvalidKeyException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (InvalidAlgorithmParameterException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		try {
			int result = cipher.update(hashKey, 0, 16, hashKey, 0);
			cipher.doFinal(hashKey, result);
		} catch (ShortBufferException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (IllegalBlockSizeException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (BadPaddingException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		ghash.reset(hashKey, 0);
		
		// Reset the nonce.
		n = 0;
	}

	@Override
	public boolean hasKey() {
		return keySpec != null;
	}

	/**
	 * Set up to encrypt or decrypt the next packet.
	 * 
	 * @param ad The associated data for the packet.
	 */
	private void setup(byte[] ad) throws InvalidKeyException, InvalidAlgorithmParameterException
	{
		// Check for nonce wrap-around.
		if (n == -1L)
			throw new IllegalStateException("Nonce has wrapped around");
		
		// Format the counter/IV block for AES/CTR/NoPadding.
		iv[0] = 0;
		iv[1] = 0;
		iv[2] = 0;
		iv[3] = 0;
		iv[4] = (byte)(n >> 56);
		iv[5] = (byte)(n >> 48);
		iv[6] = (byte)(n >> 40);
		iv[7] = (byte)(n >> 32);
		iv[8] = (byte)(n >> 24);
		iv[9] = (byte)(n >> 16);
		iv[10] = (byte)(n >> 8);
		iv[11] = (byte)n;
		iv[12] = 0;
		iv[13] = 0;
		iv[14] = 0;
		iv[15] = 1;
		++n;
		
		// Initialize the CTR mode cipher with the key and IV.
		cipher.init(Cipher.ENCRYPT_MODE, keySpec, new IvParameterSpec(iv));
		
		// Encrypt a block of zeroes to generate the hash key to XOR
		// the GHASH tag with at the end of the encrypt/decrypt operation.
		Arrays.fill(hashKey, (byte)0);
		try {
			cipher.update(hashKey, 0, 16, hashKey, 0);
		} catch (ShortBufferException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		
		// Initialize the GHASH with the associated data value.
		ghash.reset();
		if (ad != null) {
			ghash.update(ad, 0, ad.length);
			ghash.pad();
		}
	}

	@Override
	public int encryptWithAd(byte[] ad, byte[] plaintext, int plaintextOffset,
			byte[] ciphertext, int ciphertextOffset, int length)
			throws ShortBufferException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (keySpec == null) {
			// The key is not set yet - return the plaintext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(plaintext, plaintextOffset, ciphertext, ciphertextOffset, length);
			return length;
		}
		if (space < 16 || length > (space - 16))
			throw new ShortBufferException();
		try {
			setup(ad);
			int result = cipher.update(plaintext, plaintextOffset, length, ciphertext, ciphertextOffset);
			cipher.doFinal(ciphertext, ciphertextOffset + result);
		} catch (InvalidKeyException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (InvalidAlgorithmParameterException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (IllegalBlockSizeException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (BadPaddingException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		ghash.update(ciphertext, ciphertextOffset, length);
		ghash.pad(ad != null ? ad.length : 0, length);
		ghash.finish(ciphertext, ciphertextOffset + length, 16);
		for (int index = 0; index < 16; ++index)
			ciphertext[ciphertextOffset + length + index] ^= hashKey[index];
		return length + 16;
	}

	@Override
	public int decryptWithAd(byte[] ad, byte[] ciphertext,
			int ciphertextOffset, byte[] plaintext, int plaintextOffset,
			int length) throws ShortBufferException, BadPaddingException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (length > space)
			throw new ShortBufferException();
		if (plaintextOffset > plaintext.length)
			space = 0;
		else
			space = plaintext.length - plaintextOffset;
		if (keySpec == null) {
			// The key is not set yet - return the ciphertext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(ciphertext, ciphertextOffset, plaintext, plaintextOffset, length);
			return length;
		}
		if (length < 16)
			Noise.throwBadTagException();
		int dataLen = length - 16;
		if (dataLen > space)
			throw new ShortBufferException();
		try {
			setup(ad);
		} catch (InvalidKeyException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (InvalidAlgorithmParameterException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		ghash.update(ciphertext, ciphertextOffset, dataLen);
		ghash.pad(ad != null ? ad.length : 0, dataLen);
		ghash.finish(iv, 0, 16);
		int temp = 0;
		for (int index = 0; index < 16; ++index)
			temp |= (hashKey[index] ^ iv[index] ^ ciphertext[ciphertextOffset + dataLen + index]);
		if ((temp & 0xFF) != 0)
			Noise.throwBadTagException();
		try {
			int result = cipher.update(ciphertext, ciphertextOffset, dataLen, plaintext, plaintextOffset);
			cipher.doFinal(plaintext, plaintextOffset + result);
		} catch (IllegalBlockSizeException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		} catch (BadPaddingException e) {
			// Shouldn't happen.
			throw new IllegalStateException(e);
		}
		return dataLen;
	}

	@Override
	public CipherState fork(byte[] key, int offset) {
		CipherState cipher;
		try {
			cipher = new AESGCMOnCtrCipherState();
		} catch (NoSuchAlgorithmException e) {
			// Shouldn't happen.
			return null;
		}
		cipher.initializeKey(key, offset);
		return cipher;
	}

	@Override
	public void setNonce(long nonce) {
		n = nonce;
	}
}
```

## `src/main/java/com/southernstorm/noise/protocol/ChaChaPolyCipherState.java` (changed lines (217, 218, 219, 220, 221, 244, 245, 246, 250, 251, 252, 253, 254))

```java
/*
 * Copyright (C) 2016 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package com.southernstorm.noise.protocol;

import java.util.Arrays;

import javax.crypto.BadPaddingException;
import javax.crypto.ShortBufferException;

import com.southernstorm.noise.crypto.ChaChaCore;
import com.southernstorm.noise.crypto.Poly1305;

/**
 * Implements the ChaChaPoly cipher for Noise.
 */
class ChaChaPolyCipherState implements CipherState {

	private Poly1305 poly;
	private int[] input;
	private int[] output;
	private byte[] polyKey;
	long n;
	private boolean haskey;
	
	/**
	 * Constructs a new cipher state for the "ChaChaPoly" algorithm.
	 */
	public ChaChaPolyCipherState()
	{
		poly = new Poly1305();
		input = new int [16];
		output = new int [16];
		polyKey = new byte [32];
		n = 0;
		haskey = false;
	}

	@Override
	public void destroy() {
		poly.destroy();
		Arrays.fill(input, 0);
		Arrays.fill(output, 0);
		Noise.destroy(polyKey);
	}

	@Override
	public String getCipherName() {
		return "ChaChaPoly";
	}

	@Override
	public int getKeyLength() {
		return 32;
	}

	@Override
	public int getMACLength() {
		return haskey ? 16 : 0;
	}

	@Override
	public void initializeKey(byte[] key, int offset) {
		ChaChaCore.initKey256(input, key, offset);
		n = 0;
		haskey = true;
	}

	@Override
	public boolean hasKey() {
		return haskey;
	}

	/**
	 * XOR's the output of ChaCha20 with a byte buffer.
	 * 
	 * @param input The input byte buffer.
	 * @param inputOffset The offset of the first input byte.
	 * @param output The output byte buffer (can be the same as the input).
	 * @param outputOffset The offset of the first output byte.
	 * @param length The number of bytes to XOR between 1 and 64.
	 * @param block The ChaCha20 output block.
	 */
	private static void xorBlock(byte[] input, int inputOffset, byte[] output, int outputOffset, int length, int[] block)
	{
		int posn = 0;
		int value;
		while (length >= 4) {
			value = block[posn++];
			output[outputOffset] = (byte)(input[inputOffset] ^ value);
			output[outputOffset + 1] = (byte)(input[inputOffset + 1] ^ (value >> 8));
			output[outputOffset + 2] = (byte)(input[inputOffset + 2] ^ (value >> 16));
			output[outputOffset + 3] = (byte)(input[inputOffset + 3] ^ (value >> 24));
			inputOffset += 4;
			outputOffset += 4;
			length -= 4;
		}
		if (length == 3) {
			value = block[posn];
			output[outputOffset] = (byte)(input[inputOffset] ^ value);
			output[outputOffset + 1] = (byte)(input[inputOffset + 1] ^ (value >> 8));
			output[outputOffset + 2] = (byte)(input[inputOffset + 2] ^ (value >> 16));
		} else if (length == 2) {
			value = block[posn];
			output[outputOffset] = (byte)(input[inputOffset] ^ value);
			output[outputOffset + 1] = (byte)(input[inputOffset + 1] ^ (value >> 8));
		} else if (length == 1) {
			value = block[posn];
			output[outputOffset] = (byte)(input[inputOffset] ^ value);
		}
	}
	
	/**
	 * Set up to encrypt or decrypt the next packet.
	 * 
	 * @param ad The associated data for the packet.
	 */
	private void setup(byte[] ad)
	{
		if (n == -1L)
			throw new IllegalStateException("Nonce has wrapped around");
		ChaChaCore.initIV(input, n++);
		ChaChaCore.hash(output, input);
		Arrays.fill(polyKey, (byte)0);
		xorBlock(polyKey, 0, polyKey, 0, 32, output);
		poly.reset(polyKey, 0);
		if (ad != null) {
			poly.update(ad, 0, ad.length);
			poly.pad();
		}
		if (++(input[12]) == 0)
			++(input[13]);
	}

	/**
	 * Puts a 64-bit integer into a buffer in little-endian order.
	 * 
	 * @param output The output buffer.
	 * @param offset The offset into the output buffer.
	 * @param value The 64-bit integer value.
	 */
	private static void putLittleEndian64(byte[] output, int offset, long value)
	{
		output[offset] = (byte)value;
		output[offset + 1] = (byte)(value >> 8);
		output[offset + 2] = (byte)(value >> 16);
		output[offset + 3] = (byte)(value >> 24);
		output[offset + 4] = (byte)(value >> 32);
		output[offset + 5] = (byte)(value >> 40);
		output[offset + 6] = (byte)(value >> 48);
		output[offset + 7] = (byte)(value >> 56);
	}

	/**
	 * Finishes up the authentication tag for a packet.
	 * 
	 * @param ad The associated data.
	 * @param length The length of the plaintext data.
	 */
	private void finish(byte[] ad, int length)
	{
		poly.pad();
		putLittleEndian64(polyKey, 0, ad != null ? ad.length : 0);
		putLittleEndian64(polyKey, 8, length);
		poly.update(polyKey, 0, 16);
		poly.finish(polyKey, 0);
	}

	/**
	 * Encrypts or decrypts a buffer of bytes for the active packet.
	 * 
	 * @param plaintext The plaintext data to be encrypted.
	 * @param plaintextOffset The offset to the first plaintext byte.
	 * @param ciphertext The ciphertext data that results from encryption.
	 * @param ciphertextOffset The offset to the first ciphertext byte.
	 * @param length The number of bytes to encrypt.
	 */
	private void encrypt(byte[] plaintext, int plaintextOffset,
			byte[] ciphertext, int ciphertextOffset, int length) {
		while (length > 0) {
			int tempLen = 64;
			if (tempLen > length)
				tempLen = length;
			ChaChaCore.hash(output, input);
			xorBlock(plaintext, plaintextOffset, ciphertext, ciphertextOffset, tempLen, output);
			if (++(input[12]) == 0)
				++(input[13]);
			plaintextOffset += tempLen;
			ciphertextOffset += tempLen;
			length -= tempLen;
		}
	}

	@Override
	public int encryptWithAd(byte[] ad, byte[] plaintext, int plaintextOffset,
			byte[] ciphertext, int ciphertextOffset, int length) throws ShortBufferException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (!haskey) {
			// The key is not set yet - return the plaintext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(plaintext, plaintextOffset, ciphertext, ciphertextOffset, length);
			return length;
		}
		if (space < 16 || length > (space - 16))
			throw new ShortBufferException();
		setup(ad);
		encrypt(plaintext, plaintextOffset, ciphertext, ciphertextOffset, length);
		poly.update(ciphertext, ciphertextOffset, length);
		finish(ad, length);
		System.arraycopy(polyKey, 0, ciphertext, ciphertextOffset + length, 16);
		return length + 16;
	}

	@Override
	public int decryptWithAd(byte[] ad, byte[] ciphertext,
			int ciphertextOffset, byte[] plaintext, int plaintextOffset,
			int length) throws ShortBufferException, BadPaddingException {
		int space;
		if (ciphertextOffset > ciphertext.length)
			space = 0;
		else
			space = ciphertext.length - ciphertextOffset;
		if (length > space)
			throw new ShortBufferException();
		if (plaintextOffset > plaintext.length)
			space = 0;
		else
			space = plaintext.length - plaintextOffset;
		if (!haskey) {
			// The key is not set yet - return the ciphertext as-is.
			if (length > space)
				throw new ShortBufferException();
			if (plaintext != ciphertext || plaintextOffset != ciphertextOffset)
				System.arraycopy(ciphertext, ciphertextOffset, plaintext, plaintextOffset, length);
			return length;
		}
		if (length < 16)
			Noise.throwBadTagException();
		int dataLen = length - 16;
		if (dataLen > space)
			throw new ShortBufferException();
		setup(ad);
		poly.update(ciphertext, ciphertextOffset, dataLen);
		finish(ad, dataLen);
		int temp = 0;
		for (int index = 0; index < 16; ++index)
			temp |= (polyKey[index] ^ ciphertext[ciphertextOffset + dataLen + index]);
		if ((temp & 0xFF) != 0)
			Noise.throwBadTagException();
		encrypt(ciphertext, ciphertextOffset, plaintext, plaintextOffset, dataLen);
		return dataLen;
	}

	@Override
	public CipherState fork(byte[] key, int offset) {
		CipherState cipher = new ChaChaPolyCipherState();
		cipher.initializeKey(key, offset);
		return cipher;
	}

	@Override
	public void setNonce(long nonce) {
		n = nonce;
	}
}
```

## `src/main/java/com/southernstorm/noise/protocol/CipherState.java` (changed lines (103, 133))

```java
/*
 * Copyright (C) 2016 Southern Storm Software, Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package com.southernstorm.noise.protocol;

import javax.crypto.BadPaddingException;
import javax.crypto.ShortBufferException;

/**
 * Interface to an authenticated cipher for use in the Noise protocol.
 *
 * CipherState objects are used to encrypt or decrypt data during a
 * session.  Once the handshake has completed, HandshakeState.split()
 * will create two CipherState objects for encrypting packets sent to
 * the other party, and decrypting packets received from the other party.
 */
public interface CipherState extends Destroyable {

	/**
	 * Gets the Noise protocol name for this cipher.
	 * 
	 * @return The cipher name.
	 */
	String getCipherName();

	/**
	 * Gets the length of the key values for this cipher.
	 * 
	 * @return The length of the key in bytes; usually 32.
	 */
	int getKeyLength();
	
	/**
	 * Gets the length of the MAC values for this cipher.
	 * 
	 * @return The length of MAC values in bytes, or zero if the
	 * key has not yet been initialized.
	 */
	int getMACLength();

	/**
	 * Initializes the key on this cipher object.
	 * 
	 * @param key Points to a buffer that contains the key.
	 * @param offset The offset of the key in the key buffer.
	 * 
	 * The key buffer must contain at least getKeyLength() bytes
	 * starting at offset.
	 * 
	 * @see #hasKey()
	 */
	void initializeKey(byte[] key, int offset);

	/**
	 * Determine if this cipher object has been configured with a key.
	 * 
	 * @return true if this cipher object has a key; false if the
	 * key has not yet been set with initializeKey().
	 * 
	 * @see #initializeKey(byte[], int)
	 */
	boolean hasKey();
	
	/**
	 * Encrypts a plaintext buffer using the cipher and a block of associated data.
	 * 
	 * @param ad The associated data, or null if there is none.
	 * @param plaintext The buffer containing the plaintext to encrypt.
	 * @param plaintextOffset The offset within the plaintext buffer of the
	 * first byte or plaintext data.
	 * @param ciphertext The buffer to place the ciphertext in.  This can
	 * be the same as the plaintext buffer.
	 * @param ciphertextOffset The first offset within the ciphertext buffer
	 * to place the ciphertext and the MAC tag.
	 * @param length The length of the plaintext.
	 * @return The length of the ciphertext plus the MAC tag, or -1 if the
	 * ciphertext buffer is not large enough to hold the result.
	 * 
	 * @throws ShortBufferException The ciphertext buffer does not have
	 * enough space to hold the ciphertext plus MAC.
	 * 
	 * @throws IllegalStateException The nonce has wrapped around.
	 * 
	 * The plaintext and ciphertext buffers can be the same for in-place
	 * encryption.  In that case, plaintextOffset must be identical to
	 * ciphertextOffset.
	 * 
	 * There must be enough space in the ciphertext buffer to accomodate
	 * length + getMACLength() bytes of data starting at ciphertextOffset.
	 */
	int encryptWithAd(byte[] ad, byte[] plaintext, int plaintextOffset, byte[] ciphertext, int ciphertextOffset, int length) throws ShortBufferException;

	/**
	 * Decrypts a ciphertext buffer using the cipher and a block of associated data.
	 * 
	 * @param ad The associated data, or null if there is none.
	 * @param ciphertext The buffer containing the ciphertext to decrypt.
	 * @param ciphertextOffset The offset within the ciphertext buffer of
	 * the first byte of ciphertext data.
	 * @param plaintext The buffer to place the plaintext in.  This can be
	 * the same as the ciphertext buffer.
	 * @param plaintextOffset The first offset within the plaintext buffer
	 * to place the plaintext.
	 * @param length The length of the incoming ciphertext plus the MAC tag.
	 * @return The length of the plaintext with the MAC tag stripped off.
	 * 
	 * @throws ShortBufferException The plaintext buffer does not have
	 * enough space to store the decrypted data.
	 * 
	 * @throws BadPaddingException The MAC value failed to verify.
	 * 
	 * @throws IllegalStateException The nonce has wrapped around.
	 * 
	 * The plaintext and ciphertext buffers can be the same for in-place
	 * decryption.  In that case, ciphertextOffset must be identical to
	 * plaintextOffset.
	 */
	int decryptWithAd(byte[] ad, byte[] ciphertext, int ciphertextOffset, byte[] plaintext, int plaintextOffset, int length) throws ShortBufferException, BadPaddingException;

	/**
	 * Creates a new instance of this cipher and initializes it with a key.
	 * 
	 * @param key The buffer containing the key.
	 * @param offset The offset into the key buffer of the first key byte.
	 * @return A new CipherState of the same class as this one.
	 */
	CipherState fork(byte[] key, int offset);
	
	/**
	 * Sets the nonce value.
	 * 
	 * @param nonce The new nonce value, which must be greater than or equal
	 * to the current value.
	 * 
	 * This function is intended for testing purposes only.  If the nonce
	 * value goes backwards then security may be compromised.
	 */
	void setNonce(long nonce);
}
```
