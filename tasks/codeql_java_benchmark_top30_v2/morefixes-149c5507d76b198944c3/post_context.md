# Patch-overlapping Java context after the fix

## `src/main/java/org/apache/sling/jcr/base/util/RepositoryAccessor.java` (changed lines (21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34, 36, 40, 58, 59, 61, 64, 79, 80, 82, 84, 98, 99, 101, 103, 119, 120, 122, 124))

```java
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package org.apache.sling.jcr.base.util;

import org.apache.jackrabbit.rmi.client.ClientRepositoryFactory;
import org.apache.jackrabbit.rmi.client.LocalAdapterFactory;

import javax.jcr.Repository;
import java.util.Hashtable;

/** 
 * Access a Repository via JNDI or RMI. 
 *
 * @deprecated No longer supported
 */
@Deprecated
public class RepositoryAccessor {

    /** Prefix for RMI Repository URLs */
    @Deprecated
    public static final String RMI_PREFIX = "rmi://";

    /** Prefix for JNDI Repository URLs */
    @Deprecated
    public static final String JNDI_PREFIX = "jndi://";

    /**
     * Name of the property that the jcr client and server bundles to override
     * their default configuration settings and connect to the specified
     * repository instead (SLING-254 and SLING-260)
     */
    public static final String REPOSITORY_URL_OVERRIDE_PROPERTY = "sling.repository.url";

    /**
     * First try to access the Repository via JNDI (unless jndiContext is null),
     * and if not successful try RMI.
     *
     * @param repositoryName JNDI name or RMI URL (must start with "rmi://") of
     *            the Repository
     * @param jndiContext if null, JNDI is not tried
     * @return a Repository, or null if not found
     * @throws UnsupportedOperationException Always throws {@code UnsupportedOperationException}
     * @deprecated No longer supported
     */
    @Deprecated
    public Repository getRepository(String repositoryName,
            Hashtable<String, Object> jndiContext) {
        throw new UnsupportedOperationException("Repository access via JNDI-context is no longer supported.");
    }

    /**
     * Acquire a Repository from the given URL
     *
     * @param url for RMI, an RMI URL. For JNDI, "jndi://", followed by the JNDI
     *            repository name, followed by a colon and a comma-separated
     *            list of JNDI context values, for example:
     *
     * <pre>
     *      jndi://jackrabbit:java.naming.factory.initial=org.SomeClass,java.naming.provider.url=http://foo.com
     * </pre>
     *
     * @return the repository for the given url
     * @throws UnsupportedOperationException Always throws {@code UnsupportedOperationException}
     * @deprecated No longer supported
     */
    @Deprecated
    public Repository getRepositoryFromURL(String url) {
        throw new UnsupportedOperationException("Repository access via URL is no longer supported.");
    }

    /**
     * Returns the <code>LocalAdapterFactory</code> used to convert Jackrabbit
     * JCR RMI remote objects to local JCR API objects.
     * <p>
     * This method returns an instance of the
     * <code>JackrabbitClientAdapterFactory</code> which allows accessing
     * Jackrabbit (or Jackrabbit-based) repositories over RMI. Extensions of
     * this class may overwrite this method to use a different implementation.
     * 
     * @return the <code>LocalAdapterFactory</code> used to convert Jackrabbit
     * JCR RMI remote objects to local JCR API objects.
     * @throws UnsupportedOperationException Always throws {@code UnsupportedOperationException}
     * @deprecated No longer supported
     */
    @Deprecated
    protected LocalAdapterFactory getLocalAdapterFactory() {
        throw new UnsupportedOperationException("Repository access via RMI is no longer supported.");
    }

    /**
     * Returns the <code>ClientRepositoryFactory</code> to access the remote
     * JCR repository over RMI.
     * <p>
     * This method creates an instance of the
     * <code>ClientRepositoryFactory</code> class initialized with the
     * <code>LocalAdapterFactory</code> returned from the
     * {@link #getLocalAdapterFactory()} method. Extensions may overwrite this
     * method to return an extension of the Jackrabbit JCR RMI
     * <code>ClientRepositoryFactory</code> class.
     * 
     * @return the <code>ClientRepositoryFactory</code> to access the remote
     * JCR repository over RMI.
     * @throws UnsupportedOperationException Always throws {@code UnsupportedOperationException}
     * @deprecated No longer supported 
     */
    @Deprecated
    protected ClientRepositoryFactory getClientRepositoryFactory() {
        throw new UnsupportedOperationException("Repository access via RMI is no longer supported.");
    }
}
```
