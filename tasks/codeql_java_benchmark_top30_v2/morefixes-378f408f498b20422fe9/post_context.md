# Patch-overlapping Java context after the fix

## `src/main/java/org/codehaus/plexus/util/Expand.java` (changed lines (114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126))

```java
package org.codehaus.plexus.util;

/*
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2002 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution, if
 *    any, must include the following acknowledgement:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.codehaus.org/)."
 *    Alternately, this acknowledgement may appear in the software itself,
 *    if and wherever such third-party acknowledgements normally appear.
 *
 * 4. The names "The Jakarta Project", "Ant", and "Apache Software
 *    Foundation" must not be used to endorse or promote products derived
 *    from this software without prior written permission. For written
 *    permission, please contact codehaus@codehaus.org.
 *
 * 5. Products derived from this software may not be called "Apache"
 *    nor may "Apache" appear in their names without prior written
 *    permission of the Apache Group.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.codehaus.org/>.
 */

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.Date;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Unzip a file.
 *
 * @author costin@dnt.ro
 * @author <a href="mailto:stefan.bodewig@epost.de">Stefan Bodewig</a>
 * @author <a href="mailto:umagesh@codehaus.org">Magesh Umasankar</a>
 * @since Ant 1.1 @ant.task category="packaging" name="unzip" name="unjar" name="unwar"
 *
 */
public class Expand {

    private File dest; // req

    private File source; // req

    private boolean overwrite = true;

    /**
     * Do the work.
     *
     * @exception Exception Thrown in unrecoverable error.
     */
    public void execute() throws Exception {
        expandFile(source, dest);
    }

    protected void expandFile(final File srcF, final File dir) throws Exception {
        // code from WarExpand
        try (ZipInputStream zis = new ZipInputStream(Files.newInputStream(srcF.toPath()))) {
            for (ZipEntry ze = zis.getNextEntry(); ze != null; ze = zis.getNextEntry()) {
                extractFile(srcF, dir, zis, ze.getName(), new Date(ze.getTime()), ze.isDirectory());
            }
        } catch (IOException ioe) {
            throw new Exception("Error while expanding " + srcF.getPath(), ioe);
        }
    }

    protected void extractFile(
            File srcF,
            File dir,
            InputStream compressedInputStream,
            String entryName,
            Date entryDate,
            boolean isDirectory)
            throws Exception {
        File f = FileUtils.resolveFile(dir, entryName);

        try {
            String canonicalDirPath = dir.getCanonicalPath();
            String canonicalFilePath = f.getCanonicalPath();

            // Ensure the file is within the target directory
            // We need to check that the canonical file path starts with the canonical directory path
            // followed by a file separator to prevent path traversal attacks
            if (!canonicalFilePath.startsWith(canonicalDirPath + File.separator)
                    && !canonicalFilePath.equals(canonicalDirPath)) {
                throw new IOException("Entry '" + entryName + "' outside the target directory.");
            }
        } catch (IOException e) {
            throw new IOException("Failed to verify entry path for '" + entryName + "'", e);
        }

        try {
            if (!overwrite && f.exists() && f.lastModified() >= entryDate.getTime()) {
                return;
            }

            // create intermediary directories - sometimes zip don't add them
            File dirF = f.getParentFile();
            dirF.mkdirs();

            if (isDirectory) {
                f.mkdirs();
            } else {
                byte[] buffer = new byte[65536];

                try (OutputStream fos = Files.newOutputStream(f.toPath())) {
                    for (int length = compressedInputStream.read(buffer);
                            length >= 0;
                            fos.write(buffer, 0, length), length = compressedInputStream.read(buffer))
                        ;
                }
            }

            f.setLastModified(entryDate.getTime());
        } catch (FileNotFoundException ex) {
            throw new Exception("Can't extract file " + srcF.getPath(), ex);
        }
    }

    /**
     * Set the destination directory. File will be unzipped into the destination directory.
     *
     * @param d Path to the directory.
     */
    public void setDest(File d) {
        this.dest = d;
    }

    /**
     * Set the path to zip-file.
     *
     * @param s Path to zip-file.
     */
    public void setSrc(File s) {
        this.source = s;
    }

    /**
     * @param b Should we overwrite files in dest, even if they are newer than the corresponding entries in the archive?
     */
    public void setOverwrite(boolean b) {
        overwrite = b;
    }
}
```

## `src/test/java/org/codehaus/plexus/util/ExpandTest.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148))

```java
package org.codehaus.plexus.util;

/*
 * Copyright The Codehaus Foundation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.io.File;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Test for {@link Expand}.
 */
class ExpandTest extends FileBasedTestCase {

    @Test
    void testZipSlipVulnerabilityWithParentDirectory() throws Exception {
        File tempDir = getTestDirectory();
        File zipFile = new File(tempDir, "malicious.zip");
        File targetDir = new File(tempDir, "extract");
        targetDir.mkdirs();

        // Create a malicious zip with path traversal
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(zipFile.toPath()))) {
            ZipEntry entry = new ZipEntry("../../evil.txt");
            zos.putNextEntry(entry);
            zos.write("malicious content".getBytes());
            zos.closeEntry();
        }

        Expand expand = new Expand();
        expand.setSrc(zipFile);
        expand.setDest(targetDir);

        // This should throw an exception, not extract the file
        assertThrows(Exception.class, () -> expand.execute());

        // Verify the file was not created outside the target directory
        File evilFile = new File(tempDir, "evil.txt");
        assertFalse(evilFile.exists(), "File should not be extracted outside target directory");
    }

    @Test
    void testZipSlipVulnerabilityWithAbsolutePath() throws Exception {
        File tempDir = getTestDirectory();
        File zipFile = new File(tempDir, "malicious-absolute.zip");
        File targetDir = new File(tempDir, "extract-abs");
        targetDir.mkdirs();

        // Create a malicious zip with absolute path
        File evilTarget = new File("/tmp/evil-absolute.txt");
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(zipFile.toPath()))) {
            ZipEntry entry = new ZipEntry(evilTarget.getAbsolutePath());
            zos.putNextEntry(entry);
            zos.write("malicious content".getBytes());
            zos.closeEntry();
        }

        Expand expand = new Expand();
        expand.setSrc(zipFile);
        expand.setDest(targetDir);

        // This should throw an exception, not extract the file
        assertThrows(Exception.class, () -> expand.execute());

        // Verify the file was not created at the absolute path
        assertFalse(evilTarget.exists(), "File should not be extracted to absolute path");
    }

    @Test
    void testZipSlipVulnerabilityWithSimilarDirectoryName() throws Exception {
        File tempDir = getTestDirectory();
        File zipFile = new File(tempDir, "malicious-similar.zip");
        File targetDir = new File(tempDir, "extract");
        targetDir.mkdirs();

        // Create a directory with a similar name to test prefix matching vulnerability
        File similarDir = new File(tempDir, "extract-evil");
        similarDir.mkdirs();

        // Create a malicious zip that tries to exploit prefix matching
        // If targetDir is /tmp/extract, this tries to write to /tmp/extract-evil/file.txt
        String maliciousPath = "../extract-evil/evil.txt";
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(zipFile.toPath()))) {
            ZipEntry entry = new ZipEntry(maliciousPath);
            zos.putNextEntry(entry);
            zos.write("malicious content".getBytes());
            zos.closeEntry();
        }

        Expand expand = new Expand();
        expand.setSrc(zipFile);
        expand.setDest(targetDir);

        // This should throw an exception, not extract the file
        assertThrows(Exception.class, () -> expand.execute());

        // Verify the file was not created in the similar directory
        File evilFile = new File(similarDir, "evil.txt");
        assertFalse(evilFile.exists(), "File should not be extracted to directory with similar name");
    }

    @Test
    void testNormalZipExtraction() throws Exception {
        File tempDir = getTestDirectory();
        File zipFile = new File(tempDir, "normal.zip");
        File targetDir = new File(tempDir, "extract-normal");
        targetDir.mkdirs();

        // Create a normal zip
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(zipFile.toPath()))) {
            ZipEntry entry = new ZipEntry("subdir/normal.txt");
            zos.putNextEntry(entry);
            zos.write("normal content".getBytes());
            zos.closeEntry();
        }

        Expand expand = new Expand();
        expand.setSrc(zipFile);
        expand.setDest(targetDir);

        // This should succeed
        expand.execute();

        // Verify the file was created in the correct location
        File normalFile = new File(targetDir, "subdir/normal.txt");
        assertTrue(normalFile.exists(), "File should be extracted to correct location");
    }
}
```
