# Patch-overlapping Java context after the fix

## `google-oauth-client/src/main/java/com/google/api/client/auth/openidconnect/Environment.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38))

```java
/*
 * Copyright 2022, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package com.google.api.client.auth.openidconnect;

class Environment {
  public String getVariable(String name) {
    return System.getenv(name);
  }
}
```

## `google-oauth-client/src/main/java/com/google/api/client/auth/openidconnect/HttpTransportFactory.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50))

```java
/*
 * Copyright 2022, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package com.google.api.client.auth.openidconnect;

import com.google.api.client.http.HttpTransport;

/**
 * A base interface for all {@link HttpTransport} factories.
 *
 * <p>Implementation must provide a public no-arg constructor. Loading of a factory implementation
 * is done via {@link java.util.ServiceLoader}.
 */
public interface HttpTransportFactory {

  /**
   * Creates a {@code HttpTransport} instance.
   *
   * @return The HttpTransport instance.
   */
  HttpTransport create();
}
```

## `google-oauth-client/src/main/java/com/google/api/client/auth/openidconnect/IdTokenVerifier.java` (changed lines (17, 18, 19, 20, 21, 22, 23, 24, 25, 28, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 57, 58, 59, 60, 61, 62, 63, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 128, 129, 130, 131, 153, 159, 160, 161, 162, 163, 164, 165, 166, 167, 221, 222, 223, 224, 225, 226, 227, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 320, 321, 322, 323, 324, 334, 335, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625))

```java
/*
 * Copyright (c) 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

package com.google.api.client.auth.openidconnect;

import com.google.api.client.http.GenericUrl;
import com.google.api.client.http.HttpRequest;
import com.google.api.client.http.HttpResponse;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.http.javanet.NetHttpTransport;
import com.google.api.client.json.GenericJson;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.client.json.webtoken.JsonWebSignature.Header;
import com.google.api.client.util.Base64;
import com.google.api.client.util.Beta;
import com.google.api.client.util.Clock;
import com.google.api.client.util.Key;
import com.google.api.client.util.Preconditions;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.cache.CacheBuilder;
import com.google.common.cache.CacheLoader;
import com.google.common.cache.LoadingCache;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.util.concurrent.UncheckedExecutionException;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.math.BigInteger;
import java.security.AlgorithmParameters;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.NoSuchAlgorithmException;
import java.security.PublicKey;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.ECPublicKeySpec;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.InvalidParameterSpecException;
import java.security.spec.RSAPublicKeySpec;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * {@link Beta} <br>
 * Thread-safe ID token verifier based on <a
 * href="http://openid.net/specs/openid-connect-basic-1_0-27.html#id.token.validation">ID Token
 * Validation</a>.
 *
 * <p>Call {@link #verify(IdToken)} to verify a ID token. This is a light-weight object, so you may
 * use a new instance for each configuration of expected issuer and trusted client IDs. Sample
 * usage:
 *
 * <pre>
 * IdTokenVerifier verifier = new IdTokenVerifier.Builder()
 * .setIssuer("issuer.example.com")
 * .setAudience(Arrays.asList("myClientId"))
 * .build();
 * ...
 * if (!verifier.verify(idToken)) {...}
 * </pre>
 *
 * The verifier validates token signature per current OpenID Connect Spec:
 * https://openid.net/specs/openid-connect-core-1_0.html#IDTokenValidation By default, method gets a
 * certificate from well-known location A request to certificate location is performed using {@link
 * com.google.api.client.http.javanet.NetHttpTransport} Either or both certificate location and
 * transport implementation can be overridden via {@link Builder}
 *
 * <pre>
 * IdTokenVerifier verifier = new IdTokenVerifier.Builder()
 * .setIssuer("issuer.example.com")
 * .setAudience(Arrays.asList("myClientId"))
 * .setHttpTransportFactory(customHttpTransportFactory)
 * .build();
 * ...
 * if (!verifier.verify(idToken)) {...}
 * </pre>
 *
 * not recommended: this check can be disabled with OAUTH_CLIENT_SKIP_SIGNATURE environment variable
 * set to true.
 *
 * <p>Note that {@link #verify(IdToken)} only implements a subset of the verification steps, mostly
 * just the MUST steps. Please read <a
 * href="http://openid.net/specs/openid-connect-basic-1_0-27.html#id.token.validation>ID Token
 * Validation</a> for the full list of verification steps.
 *
 * @since 1.16
 */
@Beta
public class IdTokenVerifier {
  private static final Logger LOGGER = Logger.getLogger(IdTokenVerifier.class.getName());
  private static final String IAP_CERT_URL = "https://www.gstatic.com/iap/verify/public_key-jwk";
  private static final String FEDERATED_SIGNON_CERT_URL =
      "https://www.googleapis.com/oauth2/v3/certs";
  private static final Set<String> SUPPORTED_ALGORITHMS = ImmutableSet.of("RS256", "ES256");
  private static final String NOT_SUPPORTED_ALGORITHM =
      "Unexpected signing algorithm %s: expected either RS256 or ES256";

  static final HttpTransport HTTP_TRANSPORT = new NetHttpTransport();
  static final String SKIP_SIGNATURE_ENV_VAR = "OAUTH_CLIENT_SKIP_SIGNATURE";
  /** Default value for seconds of time skew to accept when verifying time (5 minutes). */
  public static final long DEFAULT_TIME_SKEW_SECONDS = 300;

  /** Clock to use for expiration checks. */
  private final Clock clock;

  private final String certificatesLocation;
  private final Environment environment;
  private final LoadingCache<String, Map<String, PublicKey>> publicKeyCache;

  /** Seconds of time skew to accept when verifying time. */
  private final long acceptableTimeSkewSeconds;

  /**
   * Unmodifiable collection of equivalent expected issuers or {@code null} to suppress the issuer
   * check.
   */
  private final Collection<String> issuers;

  /**
   * Unmodifiable list of trusted audience client IDs or {@code null} to suppress the audience
   * check.
   */
  private final Collection<String> audience;

  public IdTokenVerifier() {
    this(new Builder());
  }

  /** @param builder builder */
  protected IdTokenVerifier(Builder builder) {
    this.certificatesLocation = builder.certificatesLocation;
    clock = builder.clock;
    acceptableTimeSkewSeconds = builder.acceptableTimeSkewSeconds;
    issuers = builder.issuers == null ? null : Collections.unmodifiableCollection(builder.issuers);
    audience =
        builder.audience == null ? null : Collections.unmodifiableCollection(builder.audience);
    HttpTransportFactory transport =
        builder.httpTransportFactory == null
            ? new DefaultHttpTransportFactory()
            : builder.httpTransportFactory;
    this.publicKeyCache =
        CacheBuilder.newBuilder()
            .expireAfterWrite(1, TimeUnit.HOURS)
            .build(new PublicKeyLoader(transport));
    this.environment = builder.environment == null ? new Environment() : builder.environment;
  }

  /** Returns the clock. */
  public final Clock getClock() {
    return clock;
  }

  /** Returns the seconds of time skew to accept when verifying time. */
  public final long getAcceptableTimeSkewSeconds() {
    return acceptableTimeSkewSeconds;
  }

  /**
   * Returns the first of equivalent expected issuers or {@code null} if issuer check suppressed.
   */
  public final String getIssuer() {
    if (issuers == null) {
      return null;
    } else {
      return issuers.iterator().next();
    }
  }

  /**
   * Returns the equivalent expected issuers or {@code null} if issuer check suppressed.
   *
   * @since 1.21.0
   */
  public final Collection<String> getIssuers() {
    return issuers;
  }

  /**
   * Returns the unmodifiable list of trusted audience client IDs or {@code null} to suppress the
   * audience check.
   */
  public final Collection<String> getAudience() {
    return audience;
  }

  /**
   * Verifies that the given ID token is valid using the cached public keys.
   *
   * <p>It verifies:
   *
   * <ul>
   *   <li>The issuer is one of {@link #getIssuers()} by calling {@link
   *       IdToken#verifyIssuer(String)}.
   *   <li>The audience is one of {@link #getAudience()} by calling {@link
   *       IdToken#verifyAudience(Collection)}.
   *   <li>The current time against the issued at and expiration time, using the {@link #getClock()}
   *       and allowing for a time skew specified in {@link #getAcceptableTimeSkewSeconds()} , by
   *       calling {@link IdToken#verifyTime(long, long)}.
   *   <li>This method verifies token signature per current OpenID Connect Spec:
   *       https://openid.net/specs/openid-connect-core-1_0.html#IDTokenValidation. By default,
   *       method gets a certificate from well-known location. A request to certificate location is
   *       performed using {@link com.google.api.client.http.javanet.NetHttpTransport} Both
   *       certificate location and transport implementation can be overridden via {@link Builder}
   *       not recommended: this check can be disabled with OAUTH_CLIENT_SKIP_SIGNATURE environment
   *       variable set to true.
   * </ul>
   *
   * <p>Overriding is allowed, but it must call the super implementation.
   *
   * @param idToken ID token
   * @return {@code true} if verified successfully or {@code false} if failed
   */
  public boolean verify(IdToken idToken) {
    boolean tokenFieldsValid =
        (issuers == null || idToken.verifyIssuer(issuers))
            && (audience == null || idToken.verifyAudience(audience))
            && idToken.verifyTime(clock.currentTimeMillis(), acceptableTimeSkewSeconds);

    if (!tokenFieldsValid) {
      return false;
    }

    try {
      return verifySignature(idToken);
    } catch (VerificationException ex) {
      LOGGER.log(
          Level.SEVERE,
          "id token signature verification failed. "
              + "Please see docs for IdTokenVerifier for default settings and configuration options",
          ex);
      return false;
    }
  }

  @VisibleForTesting
  boolean verifySignature(IdToken idToken) throws VerificationException {
    if (Boolean.parseBoolean(environment.getVariable(SKIP_SIGNATURE_ENV_VAR))) {
      return true;
    }

    // Short-circuit signature types
    if (!SUPPORTED_ALGORITHMS.contains(idToken.getHeader().getAlgorithm())) {
      throw new VerificationException(
          String.format(NOT_SUPPORTED_ALGORITHM, idToken.getHeader().getAlgorithm()));
    }

    PublicKey publicKeyToUse = null;
    try {
      String certificateLocation = getCertificateLocation(idToken.getHeader());
      publicKeyToUse = publicKeyCache.get(certificateLocation).get(idToken.getHeader().getKeyId());
    } catch (ExecutionException | UncheckedExecutionException e) {
      throw new VerificationException(
          "Error fetching PublicKey from certificate location " + certificatesLocation, e);
    }

    if (publicKeyToUse == null) {
      throw new VerificationException(
          "Could not find PublicKey for provided keyId: " + idToken.getHeader().getKeyId());
    }

    try {
      if (idToken.verifySignature(publicKeyToUse)) {
        return true;
      }
      throw new VerificationException("Invalid signature");
    } catch (GeneralSecurityException e) {
      throw new VerificationException("Error validating token", e);
    }
  }

  private String getCertificateLocation(Header header) throws VerificationException {
    if (certificatesLocation != null) return certificatesLocation;

    switch (header.getAlgorithm()) {
      case "RS256":
        return FEDERATED_SIGNON_CERT_URL;
      case "ES256":
        return IAP_CERT_URL;
    }

    throw new VerificationException(String.format(NOT_SUPPORTED_ALGORITHM, header.getAlgorithm()));
  }

  /**
   * {@link Beta} <br>
   * Builder for {@link IdTokenVerifier}.
   *
   * <p>Implementation is not thread-safe.
   *
   * @since 1.16
   */
  @Beta
  public static class Builder {

    /** Clock. */
    Clock clock = Clock.SYSTEM;

    String certificatesLocation;

    /** wrapper for environment variables */
    Environment environment;

    /** Seconds of time skew to accept when verifying time. */
    long acceptableTimeSkewSeconds = DEFAULT_TIME_SKEW_SECONDS;

    /** Collection of equivalent expected issuers or {@code null} to suppress the issuer check. */
    Collection<String> issuers;

    /** List of trusted audience client IDs or {@code null} to suppress the audience check. */
    Collection<String> audience;

    HttpTransportFactory httpTransportFactory;

    /** Builds a new instance of {@link IdTokenVerifier}. */
    public IdTokenVerifier build() {
      return new IdTokenVerifier(this);
    }

    /** Returns the clock. */
    public final Clock getClock() {
      return clock;
    }

    /**
     * Sets the clock.
     *
     * <p>Overriding is only supported for the purpose of calling the super implementation and
     * changing the return type, but nothing else.
     */
    public Builder setClock(Clock clock) {
      this.clock = Preconditions.checkNotNull(clock);
      return this;
    }

    /**
     * Returns the first of equivalent expected issuers or {@code null} if issuer check suppressed.
     */
    public final String getIssuer() {
      if (issuers == null) {
        return null;
      } else {
        return issuers.iterator().next();
      }
    }

    /**
     * Sets the expected issuer or {@code null} to suppress the issuer check.
     *
     * <p>Overriding is only supported for the purpose of calling the super implementation and
     * changing the return type, but nothing else.
     */
    public Builder setIssuer(String issuer) {
      if (issuer == null) {
        return setIssuers(null);
      } else {
        return setIssuers(Collections.singleton(issuer));
      }
    }

    /**
     * Override the location URL that contains published public keys. Defaults to well-known Google
     * locations.
     *
     * @param certificatesLocation URL to published public keys
     * @return the builder
     */
    public Builder setCertificatesLocation(String certificatesLocation) {
      this.certificatesLocation = certificatesLocation;
      return this;
    }

    /**
     * Returns the equivalent expected issuers or {@code null} if issuer check suppressed.
     *
     * @since 1.21.0
     */
    public final Collection<String> getIssuers() {
      return issuers;
    }

    /**
     * Sets the list of equivalent expected issuers or {@code null} to suppress the issuer check.
     * Typically only a single issuer should be used, but multiple may be specified to support an
     * issuer transitioning to a new string. The collection must not be empty.
     *
     * <p>Overriding is only supported for the purpose of calling the super implementation and
     * changing the return type, but nothing else.
     *
     * @since 1.21.0
     */
    public Builder setIssuers(Collection<String> issuers) {
      Preconditions.checkArgument(
          issuers == null || !issuers.isEmpty(), "Issuers must not be empty");
      this.issuers = issuers;
      return this;
    }

    /**
     * Returns the list of trusted audience client IDs or {@code null} to suppress the audience
     * check.
     */
    public final Collection<String> getAudience() {
      return audience;
    }

    /**
     * Sets the list of trusted audience client IDs or {@code null} to suppress the audience check.
     *
     * <p>Overriding is only supported for the purpose of calling the super implementation and
     * changing the return type, but nothing else.
     */
    public Builder setAudience(Collection<String> audience) {
      this.audience = audience;
      return this;
    }

    /** Returns the seconds of time skew to accept when verifying time. */
    public final long getAcceptableTimeSkewSeconds() {
      return acceptableTimeSkewSeconds;
    }

    /**
     * Sets the seconds of time skew to accept when verifying time (default is {@link
     * #DEFAULT_TIME_SKEW_SECONDS}).
     *
     * <p>It must be greater or equal to zero.
     *
     * <p>Overriding is only supported for the purpose of calling the super implementation and
     * changing the return type, but nothing else.
     */
    public Builder setAcceptableTimeSkewSeconds(long acceptableTimeSkewSeconds) {
      Preconditions.checkArgument(acceptableTimeSkewSeconds >= 0);
      this.acceptableTimeSkewSeconds = acceptableTimeSkewSeconds;
      return this;
    }

    /** Returns an instance of the {@link Environment} */
    final Environment getEnvironment() {
      return environment;
    }

    /** Sets the environment. Used mostly for testing */
    Builder setEnvironment(Environment environment) {
      this.environment = environment;
      return this;
    }

    /**
     * Sets the HttpTransportFactory used for requesting public keys from the certificate URL. Used
     * mostly for testing.
     *
     * @param httpTransportFactory the HttpTransportFactory used to build certificate URL requests
     * @return the builder
     */
    public Builder setHttpTransportFactory(HttpTransportFactory httpTransportFactory) {
      this.httpTransportFactory = httpTransportFactory;
      return this;
    }
  }

  /** Custom CacheLoader for mapping certificate urls to the contained public keys. */
  static class PublicKeyLoader extends CacheLoader<String, Map<String, PublicKey>> {
    private final HttpTransportFactory httpTransportFactory;

    /**
     * Data class used for deserializing a JSON Web Key Set (JWKS) from an external HTTP request.
     */
    public static class JsonWebKeySet extends GenericJson {
      @Key public List<JsonWebKey> keys;
    }

    /** Data class used for deserializing a single JSON Web Key. */
    public static class JsonWebKey {
      @Key public String alg;

      @Key public String crv;

      @Key public String kid;

      @Key public String kty;

      @Key public String use;

      @Key public String x;

      @Key public String y;

      @Key public String e;

      @Key public String n;
    }

    PublicKeyLoader(HttpTransportFactory httpTransportFactory) {
      super();
      this.httpTransportFactory = httpTransportFactory;
    }

    @Override
    public Map<String, PublicKey> load(String certificateUrl) throws Exception {
      HttpTransport httpTransport = httpTransportFactory.create();
      JsonWebKeySet jwks;
      try {
        HttpRequest request =
            httpTransport
                .createRequestFactory()
                .buildGetRequest(new GenericUrl(certificateUrl))
                .setParser(GsonFactory.getDefaultInstance().createJsonObjectParser());
        HttpResponse response = request.execute();
        jwks = response.parseAs(JsonWebKeySet.class);
      } catch (IOException io) {
        LOGGER.log(
            Level.WARNING,
            "Failed to get a certificate from certificate location " + certificateUrl,
            io);
        return ImmutableMap.of();
      }

      ImmutableMap.Builder<String, PublicKey> keyCacheBuilder = new ImmutableMap.Builder<>();
      if (jwks.keys == null) {
        // Fall back to x509 formatted specification
        for (String keyId : jwks.keySet()) {
          String publicKeyPem = (String) jwks.get(keyId);
          keyCacheBuilder.put(keyId, buildPublicKey(publicKeyPem));
        }
      } else {
        for (JsonWebKey key : jwks.keys) {
          try {
            keyCacheBuilder.put(key.kid, buildPublicKey(key));
          } catch (NoSuchAlgorithmException
              | InvalidKeySpecException
              | InvalidParameterSpecException ignored) {
            LOGGER.log(Level.WARNING, "Failed to put a key into the cache", ignored);
          }
        }
      }

      return keyCacheBuilder.build();
    }

    private PublicKey buildPublicKey(JsonWebKey key)
        throws NoSuchAlgorithmException, InvalidParameterSpecException, InvalidKeySpecException {
      if ("ES256".equals(key.alg)) {
        return buildEs256PublicKey(key);
      } else if ("RS256".equals((key.alg))) {
        return buildRs256PublicKey(key);
      } else {
        return null;
      }
    }

    private PublicKey buildPublicKey(String publicPem)
        throws CertificateException, UnsupportedEncodingException {
      return CertificateFactory.getInstance("X.509")
          .generateCertificate(new ByteArrayInputStream(publicPem.getBytes("UTF-8")))
          .getPublicKey();
    }

    private PublicKey buildRs256PublicKey(JsonWebKey key)
        throws NoSuchAlgorithmException, InvalidKeySpecException {
      com.google.common.base.Preconditions.checkArgument("RSA".equals(key.kty));
      com.google.common.base.Preconditions.checkNotNull(key.e);
      com.google.common.base.Preconditions.checkNotNull(key.n);

      BigInteger modulus = new BigInteger(1, Base64.decodeBase64(key.n));
      BigInteger exponent = new BigInteger(1, Base64.decodeBase64(key.e));

      RSAPublicKeySpec spec = new RSAPublicKeySpec(modulus, exponent);
      KeyFactory factory = KeyFactory.getInstance("RSA");
      return factory.generatePublic(spec);
    }

    private PublicKey buildEs256PublicKey(JsonWebKey key)
        throws NoSuchAlgorithmException, InvalidParameterSpecException, InvalidKeySpecException {
      com.google.common.base.Preconditions.checkArgument("EC".equals(key.kty));
      com.google.common.base.Preconditions.checkArgument("P-256".equals(key.crv));

      BigInteger x = new BigInteger(1, Base64.decodeBase64(key.x));
      BigInteger y = new BigInteger(1, Base64.decodeBase64(key.y));
      ECPoint pubPoint = new ECPoint(x, y);
      AlgorithmParameters parameters = AlgorithmParameters.getInstance("EC");
      parameters.init(new ECGenParameterSpec("secp256r1"));
      ECParameterSpec ecParameters = parameters.getParameterSpec(ECParameterSpec.class);
      ECPublicKeySpec pubSpec = new ECPublicKeySpec(pubPoint, ecParameters);
      KeyFactory kf = KeyFactory.getInstance("EC");
      return kf.generatePublic(pubSpec);
    }
  }

  /** Custom exception for wrapping all verification errors. */
  static class VerificationException extends Exception {
    public VerificationException(String message) {
      super(message);
    }

    public VerificationException(String message, Throwable cause) {
      super(message, cause);
    }
  }

  static class DefaultHttpTransportFactory implements HttpTransportFactory {
    public HttpTransport create() {
      return HTTP_TRANSPORT;
    }
  }
}
```

## `google-oauth-client/src/test/java/com/google/api/client/auth/openidconnect/IdTokenVerifierTest.java` (changed lines (18, 19, 20, 21, 22, 23, 24, 26, 27, 28, 31, 32, 33, 34, 35, 38, 40, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 93, 94, 104, 105, 106, 107, 113, 115, 117, 118, 119, 174, 177, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347))

```java
/*
 * Copyright (c) 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

package com.google.api.client.auth.openidconnect;

import com.google.api.client.auth.openidconnect.IdToken.Payload;
import com.google.api.client.auth.openidconnect.IdTokenVerifier.VerificationException;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.http.LowLevelHttpRequest;
import com.google.api.client.http.LowLevelHttpResponse;
import com.google.api.client.http.javanet.NetHttpTransport;
import com.google.api.client.json.JsonFactory;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.client.json.webtoken.JsonWebSignature.Header;
import com.google.api.client.testing.http.MockHttpTransport;
import com.google.api.client.testing.http.MockLowLevelHttpRequest;
import com.google.api.client.testing.http.MockLowLevelHttpResponse;
import com.google.api.client.util.Clock;
import com.google.api.client.util.Lists;
import com.google.common.io.CharStreams;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import junit.framework.TestCase;

/**
 * Tests {@link IdTokenVerifier}.
 *
 * @author Yaniv Inbar
 */
public class IdTokenVerifierTest extends TestCase {

  private static final String CLIENT_ID = "myclientid";
  private static final String CLIENT_ID2 = CLIENT_ID + "2";

  private static final List<String> TRUSTED_CLIENT_IDS = Arrays.asList(CLIENT_ID, CLIENT_ID2);

  private static final String ISSUER = "issuer.example.com";
  private static final String ISSUER2 = ISSUER + "2";
  private static final String ISSUER3 = ISSUER + "3";

  private static final String ES256_TOKEN =
      "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6Im1wZjBEQSJ9.eyJhdWQiOiIvcHJvamVjdHMvNjUyNTYyNzc2Nzk4L2FwcHMvY2xvdWQtc2FtcGxlcy10ZXN0cy1waHAtaWFwIiwiZW1haWwiOiJjaGluZ29yQGdvb2dsZS5jb20iLCJleHAiOjE1ODQwNDc2MTcsImdvb2dsZSI6eyJhY2Nlc3NfbGV2ZWxzIjpbImFjY2Vzc1BvbGljaWVzLzUxODU1MTI4MDkyNC9hY2Nlc3NMZXZlbHMvcmVjZW50U2VjdXJlQ29ubmVjdERhdGEiLCJhY2Nlc3NQb2xpY2llcy81MTg1NTEyODA5MjQvYWNjZXNzTGV2ZWxzL3Rlc3ROb09wIiwiYWNjZXNzUG9saWNpZXMvNTE4NTUxMjgwOTI0L2FjY2Vzc0xldmVscy9ldmFwb3JhdGlvblFhRGF0YUZ1bGx5VHJ1c3RlZCJdfSwiaGQiOiJnb29nbGUuY29tIiwiaWF0IjoxNTg0MDQ3MDE3LCJpc3MiOiJodHRwczovL2Nsb3VkLmdvb2dsZS5jb20vaWFwIiwic3ViIjoiYWNjb3VudHMuZ29vZ2xlLmNvbToxMTIxODE3MTI3NzEyMDE5NzI4OTEifQ.yKNtdFY5EKkRboYNexBdfugzLhC3VuGyFcuFYA8kgpxMqfyxa41zkML68hYKrWu2kOBTUW95UnbGpsIi_u1fiA";

  private static final String FEDERATED_SIGNON_RS256_TOKEN =
      "eyJhbGciOiJSUzI1NiIsImtpZCI6ImY5ZDk3YjRjYWU5MGJjZDc2YWViMjAwMjZmNmI3NzBjYWMyMjE3ODMiLCJ0eXAiOiJKV1QifQ.eyJhdWQiOiJodHRwczovL2V4YW1wbGUuY29tL3BhdGgiLCJhenAiOiJpbnRlZ3JhdGlvbi10ZXN0c0BjaGluZ29yLXRlc3QuaWFtLmdzZXJ2aWNlYWNjb3VudC5jb20iLCJlbWFpbCI6ImludGVncmF0aW9uLXRlc3RzQGNoaW5nb3ItdGVzdC5pYW0uZ3NlcnZpY2VhY2NvdW50LmNvbSIsImVtYWlsX3ZlcmlmaWVkIjp0cnVlLCJleHAiOjE1ODc2Mjk4ODgsImlhdCI6MTU4NzYyNjI4OCwiaXNzIjoiaHR0cHM6Ly9hY2NvdW50cy5nb29nbGUuY29tIiwic3ViIjoiMTA0MDI5MjkyODUzMDk5OTc4MjkzIn0.Pj4KsJh7riU7ZIbPMcHcHWhasWEcbVjGP4yx_5E0iOpeDalTdri97E-o0dSSkuVX2FeBIgGUg_TNNgJ3YY97T737jT5DUYwdv6M51dDlLmmNqlu_P6toGCSRC8-Beu5gGmqS2Y82TmpHH9Vhoh5PsK7_rVHk8U6VrrVVKKTWm_IzTFhqX1oYKPdvfyaNLsXPbCt_NFE0C3DNmFkgVhRJu7LtzQQN-ghaqd3Ga3i6KH222OEI_PU4BUTvEiNOqRGoMlT_YOsyFN3XwqQ6jQGWhhkArL1z3CG2BVQjHTKpgVsRyy_H6WTZiju2Q-XWobgH-UPSZbyymV8-cFT9XKEtZQ";
  private static final String LEGACY_FEDERATED_SIGNON_CERT_URL =
      "https://www.googleapis.com/oauth2/v1/certs";

  private static final String SERVICE_ACCOUNT_RS256_TOKEN =
      "eyJhbGciOiJSUzI1NiIsImtpZCI6IjJlZjc3YjM4YTFiMDM3MDQ4NzA0MzkxNmFjYmYyN2Q3NGVkZDA4YjEiLCJ0eXAiOiJKV1QifQ.eyJhdWQiOiJodHRwczovL2V4YW1wbGUuY29tL2F1ZGllbmNlIiwiZXhwIjoxNTg3NjMwNTQzLCJpYXQiOjE1ODc2MjY5NDMsImlzcyI6InNvbWUgaXNzdWVyIiwic3ViIjoic29tZSBzdWJqZWN0In0.gGOQW0qQgs4jGUmCsgRV83RqsJLaEy89-ZOG6p1u0Y26FyY06b6Odgd7xXLsSTiiSnch62dl0Lfi9D0x2ByxvsGOCbovmBl2ZZ0zHr1wpc4N0XS9lMUq5RJQbonDibxXG4nC2zroDfvD0h7i-L8KMXeJb9pYwW7LkmrM_YwYfJnWnZ4bpcsDjojmPeUBlACg7tjjOgBFbyQZvUtaERJwSRlaWibvNjof7eCVfZChE0PwBpZc_cGqSqKXv544L4ttqdCnmONjqrTATXwC4gYxruevkjHfYI5ojcQmXoWDJJ0-_jzfyPE4MFFdCFgzLgnfIOwe5ve0MtquKuv2O0pgvg";
  private static final String SERVICE_ACCOUNT_CERT_URL =
      "https://www.googleapis.com/robot/v1/metadata/x509/integration-tests%40chingor-test.iam.gserviceaccount.com";

  private static final List<String> ALL_TOKENS =
      Arrays.asList(ES256_TOKEN, FEDERATED_SIGNON_RS256_TOKEN, SERVICE_ACCOUNT_RS256_TOKEN);

  static final JsonFactory JSON_FACTORY = GsonFactory.getDefaultInstance();
  static final MockClock FIXED_CLOCK = new MockClock(1584047020000L);

  private static IdToken newIdToken(String issuer, String audience) {
    Payload payload = new Payload();
    payload.setIssuer(issuer);
    payload.setAudience(audience);
    payload.setExpirationTimeSeconds(2000L);
    payload.setIssuedAtTimeSeconds(1000L);
    return new IdToken(new Header(), payload, new byte[0], new byte[0]);
  }

  public void testBuilder() throws Exception {
    IdTokenVerifier.Builder builder =
        new IdTokenVerifier.Builder().setIssuer(ISSUER).setAudience(TRUSTED_CLIENT_IDS);
    assertEquals(Clock.SYSTEM, builder.getClock());
    assertEquals(ISSUER, builder.getIssuer());
    assertEquals(Collections.singleton(ISSUER), builder.getIssuers());
    assertEquals(TRUSTED_CLIENT_IDS, builder.getAudience());
    Clock clock = new MockClock();
    builder.setClock(clock);
    assertEquals(clock, builder.getClock());
    IdTokenVerifier verifier = builder.build();
    assertEquals(clock, verifier.getClock());
    assertEquals(ISSUER, verifier.getIssuer());
    assertEquals(Collections.singleton(ISSUER), builder.getIssuers());
    assertEquals(TRUSTED_CLIENT_IDS, Lists.newArrayList(verifier.getAudience()));
  }

  public void testVerify() throws Exception {
    MockClock clock = new MockClock();
    MockEnvironment testEnvironment = new MockEnvironment();
    testEnvironment.setVariable(IdTokenVerifier.SKIP_SIGNATURE_ENV_VAR, "true");
    IdTokenVerifier verifier =
        new IdTokenVerifier.Builder()
            .setIssuers(Arrays.asList(ISSUER, ISSUER3))
            .setAudience(Arrays.asList(CLIENT_ID))
            .setClock(clock)
            .setEnvironment(testEnvironment)
            .build();

    // verifier flexible doesn't check issuer and audience
    IdTokenVerifier verifierFlexible =
        new IdTokenVerifier.Builder().setClock(clock).setEnvironment(testEnvironment).build();

    // issuer
    clock.timeMillis = 1500000L;
    IdToken idToken = newIdToken(ISSUER, CLIENT_ID);
    assertTrue(verifier.verify(idToken));
    assertTrue(verifierFlexible.verify(newIdToken(ISSUER2, CLIENT_ID)));
    assertFalse(verifier.verify(newIdToken(ISSUER2, CLIENT_ID)));
    assertTrue(verifier.verify(newIdToken(ISSUER3, CLIENT_ID)));
    // audience
    assertTrue(verifierFlexible.verify(newIdToken(ISSUER, CLIENT_ID2)));
    assertFalse(verifier.verify(newIdToken(ISSUER, CLIENT_ID2)));
    // time
    clock.timeMillis = 700000L;
    assertTrue(verifier.verify(idToken));
    clock.timeMillis = 2300000L;
    assertTrue(verifier.verify(idToken));
    clock.timeMillis = 699999L;
    assertFalse(verifier.verify(idToken));
    clock.timeMillis = 2300001L;
    assertFalse(verifier.verify(idToken));
  }

  public void testEmptyIssuersFails() throws Exception {
    IdTokenVerifier.Builder builder = new IdTokenVerifier.Builder();
    try {
      builder.setIssuers(Collections.<String>emptyList());
      fail("Exception expected");
    } catch (IllegalArgumentException ex) {
      // Expected
    }
  }

  public void testBuilderSetNullIssuers() throws Exception {
    IdTokenVerifier.Builder builder = new IdTokenVerifier.Builder();
    IdTokenVerifier verifier = builder.build();
    assertNull(builder.getIssuers());
    assertNull(builder.getIssuer());
    assertNull(verifier.getIssuers());
    assertNull(verifier.getIssuer());

    builder.setIssuers(null);
    verifier = builder.build();
    assertNull(builder.getIssuers());
    assertNull(builder.getIssuer());
    assertNull(verifier.getIssuers());
    assertNull(verifier.getIssuer());

    builder.setIssuer(null);
    verifier = builder.build();
    assertNull(builder.getIssuers());
    assertNull(builder.getIssuer());
    assertNull(verifier.getIssuers());
    assertNull(verifier.getIssuer());
  }

  public void testMissingAudience() throws VerificationException {
    IdToken idToken = newIdToken(ISSUER, null);

    MockClock clock = new MockClock();
    clock.timeMillis = 1500000L;
    IdTokenVerifier verifier =
        new IdTokenVerifier.Builder()
            .setIssuers(Arrays.asList(ISSUER, ISSUER3))
            .setAudience(Collections.<String>emptyList())
            .setClock(clock)
            .build();
    assertFalse(verifier.verify(idToken));
  }

  public void testVerifyEs256TokenPublicKeyMismatch() throws Exception {
    // Mock HTTP requests
    HttpTransportFactory httpTransportFactory =
        new HttpTransportFactory() {
          @Override
          public HttpTransport create() {
            return new MockHttpTransport() {
              @Override
              public LowLevelHttpRequest buildRequest(String method, String url)
                  throws IOException {
                return new MockLowLevelHttpRequest() {
                  @Override
                  public LowLevelHttpResponse execute() throws IOException {
                    MockLowLevelHttpResponse response = new MockLowLevelHttpResponse();
                    response.setStatusCode(200);
                    response.setContentType("application/json");
                    response.setContent("");
                    return response;
                  }
                };
              }
            };
          }
        };
    IdTokenVerifier tokenVerifier =
        new IdTokenVerifier.Builder()
            .setClock(FIXED_CLOCK)
            .setHttpTransportFactory(httpTransportFactory)
            .build();

    try {
      tokenVerifier.verifySignature(IdToken.parse(JSON_FACTORY, ES256_TOKEN));
      fail("Should have failed verification");
    } catch (VerificationException ex) {
      assertTrue(ex.getMessage().contains("Error fetching PublicKey"));
    }
  }

  public void testVerifyEs256Token() throws VerificationException, IOException {
    HttpTransportFactory httpTransportFactory =
        mockTransport(
            "https://www.gstatic.com/iap/verify/public_key-jwk",
            readResourceAsString("iap_keys.json"));
    IdTokenVerifier tokenVerifier =
        new IdTokenVerifier.Builder()
            .setClock(FIXED_CLOCK)
            .setHttpTransportFactory(httpTransportFactory)
            .build();
    assertTrue(tokenVerifier.verify(IdToken.parse(JSON_FACTORY, ES256_TOKEN)));
  }

  public void testVerifyRs256Token() throws VerificationException, IOException {
    HttpTransportFactory httpTransportFactory =
        mockTransport(
            "https://www.googleapis.com/oauth2/v3/certs",
            readResourceAsString("federated_keys.json"));
    MockClock clock = new MockClock(1587625988000L);
    IdTokenVerifier tokenVerifier =
        new IdTokenVerifier.Builder()
            .setClock(clock)
            .setHttpTransportFactory(httpTransportFactory)
            .build();
    assertTrue(tokenVerifier.verify(IdToken.parse(JSON_FACTORY, FEDERATED_SIGNON_RS256_TOKEN)));
  }

  public void testVerifyRs256TokenWithLegacyCertificateUrlFormat()
      throws VerificationException, IOException {
    HttpTransportFactory httpTransportFactory =
        mockTransport(
            LEGACY_FEDERATED_SIGNON_CERT_URL, readResourceAsString("legacy_federated_keys.json"));
    MockClock clock = new MockClock(1587626288000L);
    IdTokenVerifier tokenVerifier =
        new IdTokenVerifier.Builder()
            .setCertificatesLocation(LEGACY_FEDERATED_SIGNON_CERT_URL)
            .setClock(clock)
            .setHttpTransportFactory(httpTransportFactory)
            .build();
    assertTrue(tokenVerifier.verify(IdToken.parse(JSON_FACTORY, FEDERATED_SIGNON_RS256_TOKEN)));
  }

  public void testVerifyServiceAccountRs256Token() throws VerificationException, IOException {
    MockClock clock = new MockClock(1587626643000L);
    IdTokenVerifier tokenVerifier =
        new IdTokenVerifier.Builder()
            .setClock(clock)
            .setCertificatesLocation(SERVICE_ACCOUNT_CERT_URL)
            .setHttpTransportFactory(new DefaultHttpTransportFactory())
            .build();
    assertTrue(tokenVerifier.verify(IdToken.parse(JSON_FACTORY, SERVICE_ACCOUNT_RS256_TOKEN)));
  }

  static String readResourceAsString(String resourceName) throws IOException {
    InputStream inputStream =
        IdTokenVerifierTest.class.getClassLoader().getResourceAsStream(resourceName);
    try (final Reader reader = new InputStreamReader(inputStream)) {
      return CharStreams.toString(reader);
    }
  }

  static HttpTransportFactory mockTransport(String url, String certificates) {
    final String certificatesContent = certificates;
    final String certificatesUrl = url;
    return new HttpTransportFactory() {
      @Override
      public HttpTransport create() {
        return new MockHttpTransport() {
          @Override
          public LowLevelHttpRequest buildRequest(String method, String url) throws IOException {
            assertEquals(certificatesUrl, url);
            return new MockLowLevelHttpRequest() {
              @Override
              public LowLevelHttpResponse execute() throws IOException {
                MockLowLevelHttpResponse response = new MockLowLevelHttpResponse();
                response.setStatusCode(200);
                response.setContentType("application/json");
                response.setContent(certificatesContent);
                return response;
              }
            };
          }
        };
      }
    };
  }

  /** A mock implementation of {@link Clock} to set clock for testing */
  static class MockClock implements Clock {
    public MockClock() {}

    public MockClock(long timeMillis) {
      this.timeMillis = timeMillis;
    }

    long timeMillis;

    public long currentTimeMillis() {
      return timeMillis;
    }
  }

  /** A default http transport factory for testing */
  static class DefaultHttpTransportFactory implements HttpTransportFactory {
    public HttpTransport create() {
      return new NetHttpTransport();
    }
  }

  /** A mock implementation of {@link Environment} to set environment variables for testing */
  class MockEnvironment extends Environment {
    private final Map<String, String> variables = new HashMap<>();

    @Override
    public String getVariable(String name) {
      return variables.get(name);
    }

    public void setVariable(String name, String value) {
      variables.put(name, value);
    }
  }
}
```
