# Patch-overlapping Java context after the fix

## `src/main/java/dev/hypera/dragonfly/Dragonfly.java` (changed lines (43, 51, 54, 55, 56, 58, 59, 60, 78, 88, 141, 145, 155))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly;

import dev.hypera.dragonfly.dependency.Dependency;
import dev.hypera.dragonfly.downloaders.DependencyDownloader;
import dev.hypera.dragonfly.loading.DependencyLoader;
import dev.hypera.dragonfly.loading.IClassLoader;
import dev.hypera.dragonfly.objects.Status;
import dev.hypera.dragonfly.relocation.DependencyRelocator;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import org.jetbrains.annotations.ApiStatus.Internal;
import org.jetbrains.annotations.NotNull;

/**
 * Main Dragonfly class.
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class Dragonfly {

	private static final @NotNull String VERSION = "0.3.1-SNAPSHOT";

	private final int timeout;
	private final @NotNull Path directory;
	private final @NotNull Set<String> repositories;
	private final @NotNull Consumer<Status> statusHandler;

	private final @NotNull DependencyDownloader dependencyDownloader = new DependencyDownloader(this);
	private final @NotNull DependencyRelocator dependencyRelocator;
	private final @NotNull DependencyLoader dependencyLoader;


	@Internal
	protected Dragonfly(int timeout, IClassLoader classLoader, Path directory, Set<String> repositories, boolean delete, Consumer<Status> statusHandler) throws IOException {
		this.timeout = timeout;
		this.directory = directory;
		this.repositories = repositories;
		this.statusHandler = statusHandler;

		this.dependencyRelocator = new DependencyRelocator(this, delete);
		this.dependencyLoader = new DependencyLoader(this, classLoader);

		if (!Files.exists(directory)) {
			Files.createDirectories(directory);
		}
	}

	public static @NotNull String getVersion() {
		return VERSION;
	}

	/**
	 * Download, relocate and load dependencies.
	 *
	 * @param dependencies Dependencies to be loaded.
	 * @return If the load was successful, in the form of a {@link CompletableFuture<Boolean>}.
	 */
	public @NotNull CompletableFuture<Boolean> load(@NotNull Dependency... dependencies) {
		return CompletableFuture.supplyAsync(() -> {
			try {
				statusHandler.accept(Status.STARTING);
				List<Dependency> dependencyList = Arrays.stream(dependencies)
						.sorted(Comparator.comparingInt(Dependency::getPriority)).collect(Collectors.toList());

				if (dependencyList.size() > 0) {
					List<Dependency> downloadList = dependencyList.stream().filter(d -> !isDownloaded(d))
							.collect(Collectors.toList());
					if (downloadList.size() > 0) {
						statusHandler.accept(Status.DOWNLOADING);
						dependencyDownloader.download(downloadList);

						if (downloadList.stream().anyMatch(d -> d.getRelocations().size() > 0)) {
							statusHandler.accept(Status.RELOCATING);
							dependencyRelocator.relocate(downloadList);
						}
					}

					dependencyList.stream().filter(d -> d.getRelocations().size() > 0 && !d.isRelocated())
							.forEach(d -> d.setFileName(dependencyRelocator.getRelocatedFileName(d)));

					statusHandler.accept(Status.LOADING);
					dependencyLoader.load(dependencyList);
				}

				statusHandler.accept(Status.FINISHED);
				return true;
			} catch (Exception ex) {
				throw new IllegalStateException(ex);
			}
		});
	}

	/**
	 * If a dependency has been downloaded or not.
	 *
	 * @param dependency Dependency.
	 * @return If the given dependency has been downloaded or not.
	 */
	private boolean isDownloaded(Dependency dependency) {
		if (dependencyRelocator.isRelocated(dependency)) {
			return true;
		} else {
			return Files.exists(directory.resolve(dependency.getFileName()));
		}
	}

	public int getTimeout() {
		return timeout;
	}

	public @NotNull Path getDirectory() {
		return directory;
	}

	public @NotNull Set<String> getRepositories() {
		return repositories;
	}

	/**
	 * Get {@link DependencyDownloader}, for internal use only.
	 *
	 * @return Stored instance of {@link DependencyDownloader}.
	 */
	@Internal
	public @NotNull DependencyDownloader getDependencyDownloader() {
		return dependencyDownloader;
	}

}
```

## `src/main/java/dev/hypera/dragonfly/DragonflyBuilder.java` (changed lines (35, 119))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly;

import dev.hypera.dragonfly.annotations.Builder;
import dev.hypera.dragonfly.loading.IClassLoader;
import dev.hypera.dragonfly.objects.Status;
import java.io.IOException;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.function.Consumer;
import org.jetbrains.annotations.Contract;
import org.jetbrains.annotations.NotNull;

/**
 * {@link Dragonfly} builder.
 * @author Joshua Sing <joshua@hypera.dev>
 */
@Builder
public class DragonflyBuilder {

	private int timeout = 5000;
	private final @NotNull IClassLoader classLoader;
	private final @NotNull Path directory;
	private final @NotNull Set<String> repositories = new HashSet<>();
	private boolean deleteOnRelocate = true;
	private @NotNull Consumer<Status> statusHandler = status -> {};

	private DragonflyBuilder(@NotNull IClassLoader classLoader, @NotNull Path directory) {
		this.classLoader = classLoader;
		this.directory = directory;
		this.repositories.add("https://repo1.maven.org/maven2/");
	}

	/**
	 * Create a new Dragonfly builder.
	 *
	 * @param classLoader Class loader, used for loading the dependencies into the class path.
	 * @param directory   Directory to save dependencies in.
	 * @return New {@link DragonflyBuilder} instance.
	 */
	public static @NotNull DragonflyBuilder create(@NotNull IClassLoader classLoader, @NotNull Path directory) {
		return new DragonflyBuilder(classLoader, directory);
	}

	/**
	 * Set http timeout.
	 *
	 * @param timeout Timeout in milliseconds.
	 * @return Current {@link DragonflyBuilder} instance.
	 */
	public @NotNull DragonflyBuilder setTimeout(int timeout) {
		this.timeout = timeout;
		return this;
	}

	/**
	 * Add repositories to resolve dependencies in.
	 *
	 * @param repositories Repositories to be added.
	 * @return Current {@link DragonflyBuilder} instance.
	 */
	public @NotNull DragonflyBuilder addRepositories(@NotNull String... repositories) {
		this.repositories.addAll(Arrays.asList(repositories));
		return this;
	}

	/**
	 * Set if a dependency with relocation's non-relocated jar should be deleted after the relocated version is
	 * created.
	 *
	 * @param deleteOnRelocate Should a relocated dependency's non-relocated jar be deleted after relocation?
	 * @return Current {@link DragonflyBuilder} instance.
	 */
	public @NotNull DragonflyBuilder setDeleteOnRelocate(boolean deleteOnRelocate) {
		this.deleteOnRelocate = deleteOnRelocate;
		return this;
	}

	/**
	 * Sets the status handler, which will be provided status updates as a load is in progress.
	 *
	 * @param statusHandler Status handler.
	 * @return Current {@link DragonflyBuilder} instance.
	 */
	public @NotNull DragonflyBuilder setStatusHandler(@NotNull Consumer<Status> statusHandler) {
		this.statusHandler = statusHandler;
		return this;
	}

	/**
	 * Builds a new Dragonfly instance using the provided settings.
	 *
	 * @return New {@link Dragonfly} instance.
	 */
	@Contract(value = "-> new", pure = true)
	public @NotNull Dragonfly build() {
		try {
			return new Dragonfly(timeout, classLoader, directory, repositories, deleteOnRelocate, statusHandler);
		} catch (IOException ex) {
			throw new IllegalStateException(ex);
		}
	}

}
```

## `src/main/java/dev/hypera/dragonfly/downloaders/impl/MavenDownloader.java` (changed lines (44, 45))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.downloaders.impl;

import dev.hypera.dragonfly.annotations.Downloader;
import dev.hypera.dragonfly.downloaders.IDownloader;
import dev.hypera.dragonfly.dependency.impl.MavenDependency;
import dev.hypera.dragonfly.exceptions.ResolveFailureException;
import dev.hypera.dragonfly.resolvers.impl.MavenResolver;
import dev.hypera.dragonfly.resolvers.impl.MavenSnapshotResolver;
import dev.hypera.dragonfly.Dragonfly;
import dev.hypera.dragonfly.exceptions.DownloadFailureException;
import org.jetbrains.annotations.NotNull;

/**
 * Maven dependency downloader.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
@Downloader(MavenDependency.class)
public class MavenDownloader implements IDownloader<MavenDependency> {

	private final @NotNull MavenResolver resolver = new MavenResolver();
	private final @NotNull MavenSnapshotResolver snapshotResolver = new MavenSnapshotResolver();

	@Override
	public void download(@NotNull Dragonfly dragonfly, @NotNull MavenDependency dependency) throws DownloadFailureException {
		String url;
		if (dependency.getVersion().contains("-SNAPSHOT")) {
			url = snapshotResolver.resolve(dragonfly, dependency);
		} else {
			url = resolver.resolve(dragonfly, dependency);
		}

		if (null == url) {
			throw new ResolveFailureException("Cannot resolve dependency: " + dependency);
		}

		download(url, dragonfly.getTimeout(), dragonfly.getDirectory().resolve(dependency.getFileName()));
	}

}
```

## `src/main/java/dev/hypera/dragonfly/exceptions/DownloadFailureException.java` (changed lines (26, 27, 30, 31, 36, 40, 44, 48))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.exceptions;

import org.jetbrains.annotations.NotNull;

public class DownloadFailureException extends DragonflyException {

	private static final long serialVersionUID = 5648475409314204882L;

	public DownloadFailureException() {
		super();
	}

	public DownloadFailureException(@NotNull String message) {
		super(message);
	}

	public DownloadFailureException(@NotNull String message, @NotNull Throwable cause) {
		super(message, cause);
	}

	public DownloadFailureException(@NotNull Throwable cause) {
		super(cause);
	}

	protected DownloadFailureException(@NotNull String message, @NotNull Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
		super(message, cause, enableSuppression, writableStackTrace);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/exceptions/DragonflyException.java` (changed lines (26, 27, 34, 35, 40, 44, 48, 52))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.exceptions;

import org.jetbrains.annotations.NotNull;

/**
 * Dragonfly Exception.
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class DragonflyException extends Exception {

	private static final long serialVersionUID = 3565376065959848642L;

	public DragonflyException() {
		super();
	}

	public DragonflyException(@NotNull String message) {
		super(message);
	}

	public DragonflyException(@NotNull String message, @NotNull Throwable cause) {
		super(message, cause);
	}

	public DragonflyException(@NotNull Throwable cause) {
		super(cause);
	}

	protected DragonflyException(@NotNull String message, @NotNull Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
		super(message, cause, enableSuppression, writableStackTrace);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/exceptions/LoadFailureException.java` (changed lines (26, 27, 30, 31, 36, 40, 44, 48))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.exceptions;

import org.jetbrains.annotations.NotNull;

public class LoadFailureException extends DragonflyException {

	private static final long serialVersionUID = -8555618454694039541L;

	public LoadFailureException() {
		super();
	}

	public LoadFailureException(@NotNull String message) {
		super(message);
	}

	public LoadFailureException(@NotNull String message, @NotNull Throwable cause) {
		super(message, cause);
	}

	public LoadFailureException(@NotNull Throwable cause) {
		super(cause);
	}

	protected LoadFailureException(@NotNull String message, @NotNull Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
		super(message, cause, enableSuppression, writableStackTrace);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/exceptions/RelocationFailureException.java` (changed lines (26, 27, 30, 31, 36, 40, 44, 48))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.exceptions;

import org.jetbrains.annotations.NotNull;

public class RelocationFailureException extends DragonflyException {

	private static final long serialVersionUID = -2817144091943660838L;

	public RelocationFailureException() {
		super();
	}

	public RelocationFailureException(@NotNull String message) {
		super(message);
	}

	public RelocationFailureException(@NotNull String message, @NotNull Throwable cause) {
		super(message, cause);
	}

	public RelocationFailureException(@NotNull Throwable cause) {
		super(cause);
	}

	protected RelocationFailureException(@NotNull String message, @NotNull Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
		super(message, cause, enableSuppression, writableStackTrace);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/exceptions/ResolveFailureException.java` (changed lines (26, 27, 30, 31, 36, 40, 44, 48))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.exceptions;

import org.jetbrains.annotations.NotNull;

public class ResolveFailureException extends DownloadFailureException {

	private static final long serialVersionUID = -8393996338726021911L;

	public ResolveFailureException() {
		super();
	}

	public ResolveFailureException(@NotNull String message) {
		super(message);
	}

	public ResolveFailureException(@NotNull String message, @NotNull Throwable cause) {
		super(message, cause);
	}

	public ResolveFailureException(@NotNull Throwable cause) {
		super(cause);
	}

	protected ResolveFailureException(@NotNull String message, @NotNull Throwable cause, boolean enableSuppression, boolean writableStackTrace) {
		super(message, cause, enableSuppression, writableStackTrace);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/loading/DependencyLoader.java` (changed lines (32, 41, 42, 45, 56, 68))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.loading;

import dev.hypera.dragonfly.Dragonfly;
import dev.hypera.dragonfly.dependency.Dependency;
import dev.hypera.dragonfly.exceptions.LoadFailureException;
import java.net.MalformedURLException;
import java.util.List;
import org.jetbrains.annotations.ApiStatus.Internal;
import org.jetbrains.annotations.NotNull;

/**
 * Dependency loader.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class DependencyLoader {

	private final @NotNull Dragonfly dragonfly;
	private final @NotNull IClassLoader classLoader;

	@Internal
	public DependencyLoader(@NotNull Dragonfly dragonfly, @NotNull IClassLoader classLoader) {
		this.dragonfly = dragonfly;
		this.classLoader = classLoader;
	}

	/**
	 * Attempt to load a list of dependencies.
	 *
	 * @param dependencies Dependencies to be loaded.
	 * @throws LoadFailureException if something went wrong while loading the dependencies.
	 */
	public void load(@NotNull List<Dependency> dependencies) throws LoadFailureException {
		for (Dependency dependency : dependencies) {
			load(dependency);
		}
	}

	/**
	 * Attempt to load a dependency.
	 *
	 * @param dependency Dependency to be loaded.
	 * @throws LoadFailureException if something went wrong while loading the dependency.
	 */
	private void load(@NotNull Dependency dependency) throws LoadFailureException {
		try {
			classLoader.addURL(dragonfly.getDirectory().resolve(dependency.getFileName()).toUri().toURL());
		} catch (MalformedURLException ex) {
			throw new LoadFailureException(ex);
		}
	}

}
```

## `src/main/java/dev/hypera/dragonfly/loading/DragonflyClassLoader.java` (changed lines (29, 39, 44, 49))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.loading;

import java.net.URL;
import java.net.URLClassLoader;
import org.jetbrains.annotations.ApiStatus.Internal;
import org.jetbrains.annotations.NotNull;

/**
 * Dragonfly class loader, a child-first {@link URLClassLoader} used for loading Dragonfly's internal dependencies.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
@Internal
public class DragonflyClassLoader extends URLClassLoader {

	public DragonflyClassLoader(@NotNull ClassLoader classLoader) {
		super(new URL[0], classLoader);
	}

	@Override
	public void addURL(@NotNull URL url) {
		super.addURL(url);
	}

	@Override
	protected @NotNull Class<?> loadClass(@NotNull String name, boolean resolve) throws ClassNotFoundException {
		Class<?> loadedClass = findLoadedClass(name);
		if (null == loadedClass) {
			try {
				loadedClass = findClass(name);
			} catch (ClassNotFoundException ex) {
				loadedClass = super.loadClass(name, resolve);
			}
		}

		if (resolve) {
			resolveClass(loadedClass);
		}

		return loadedClass;
	}

}
```

## `src/main/java/dev/hypera/dragonfly/relocation/DependencyRelocator.java` (changed lines (44, 53, 55, 58, 59, 63, 64, 67))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.relocation;

import dev.hypera.dragonfly.Dragonfly;
import dev.hypera.dragonfly.dependency.Dependency;
import dev.hypera.dragonfly.exceptions.DownloadFailureException;
import dev.hypera.dragonfly.exceptions.LoadFailureException;
import dev.hypera.dragonfly.exceptions.RelocationFailureException;
import dev.hypera.dragonfly.loading.DependencyLoader;
import dev.hypera.dragonfly.loading.DragonflyClassLoader;
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.jetbrains.annotations.ApiStatus.Internal;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Dependency relocator.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class DependencyRelocator {

	private static final @NotNull String RELOCATED_FILENAME = "%s-relocated.jar";

	private final @NotNull Dragonfly dragonfly;
	private final boolean deleteOld;

	private final @NotNull DragonflyClassLoader classLoader;
	private final @NotNull DependencyLoader dependencyLoader;

	private boolean dependenciesLoaded;

	private @Nullable Constructor<?> constructor;
	private @Nullable Method method;

	@Internal
	public DependencyRelocator(@NotNull Dragonfly dragonfly, boolean deleteOld) {
		this.dragonfly = dragonfly;
		this.deleteOld = deleteOld;
		this.classLoader = new DragonflyClassLoader(getClass().getClassLoader());
		this.dependencyLoader = new DependencyLoader(dragonfly, classLoader::addURL);
	}


	/**
	 * Initialise relocator.
	 *
	 * @throws RelocationFailureException if something went wrong while initialising.
	 */
	private void init() throws RelocationFailureException {
		if (null == constructor && null == method) {
			try {
				Class<?> clazz = classLoader.loadClass("me.lucko.jarrelocator.JarRelocator");

				constructor = clazz.getDeclaredConstructor(File.class, File.class, Map.class);
				method = clazz.getDeclaredMethod("run");

				constructor.setAccessible(true);
				method.setAccessible(true);
			} catch (Exception ex) {
				throw new RelocationFailureException(ex);
			}
		}
	}


	/**
	 * Attempt to relocate a list of dependencies.
	 *
	 * @param dependencies Dependencies to be relocated.
	 * @throws RelocationFailureException if something went wrong while relocating the dependencies.
	 * @throws DownloadFailureException   if something went wrong while downloading internal dependencies.
	 * @throws LoadFailureException       if something went wrong while loading the internal dependencies.
	 */
	public void relocate(@NotNull List<Dependency> dependencies) throws RelocationFailureException, DownloadFailureException, LoadFailureException {
		loadInternalDependencies(getDependencies());
		init();

		for (Dependency dependency : dependencies) {
			if (dependency.getRelocations().size() > 0) {
				relocate(dependency);
			}
		}
	}


	/**
	 * Attempt to relocate a dependency.
	 *
	 * @param dependency Dependency to be relocated.
	 * @throws RelocationFailureException if something went wrong while relocating the dependency.
	 */
	private void relocate(@NotNull Dependency dependency) throws RelocationFailureException {
		try {
			Path relocatedPath = getRelocatedPath(dependency);
			if (Files.exists(relocatedPath)) {
				dependency.setFileName(getRelocatedFileName(dependency));
				dependency.setRelocated(true);
				return;
			}

			Map<String, String> relocations = new HashMap<>();
			dependency.getRelocations().forEach(r -> relocations.put(r.getFrom(), r.getTo()));

			Object object = constructor.newInstance(dragonfly.getDirectory().resolve(dependency.getFileName())
					.toFile(), relocatedPath.toFile(), relocations);
			method.invoke(object);

			if (deleteOld) {
				Files.delete(dragonfly.getDirectory().resolve(dependency.getFileName()));
			}

			dependency.setFileName(getRelocatedFileName(dependency));
			dependency.setRelocated(true);
		} catch (Exception ex) {
			throw new RelocationFailureException(ex);
		}
	}

	/**
	 * If a dependency has been relocated or not.
	 *
	 * @param dependency Dependency.
	 * @return If the given dependency has been relocated or not.
	 */
	public boolean isRelocated(@NotNull Dependency dependency) {
		if (dependency.getRelocations().size() > 0) {
			return Files.exists(getRelocatedPath(dependency));
		} else {
			return false;
		}
	}

	/**
	 * Get the relocated path of a dependency.
	 *
	 * @param dependency Dependency to get the relocated path of.
	 * @return Relocated path of the given dependency.
	 */
	public @NotNull Path getRelocatedPath(@NotNull Dependency dependency) {
		return dragonfly.getDirectory().resolve(getRelocatedFileName(dependency));
	}

	/**
	 * Get the relocated filename of a dependency.
	 *
	 * @param dependency Dependency to get the relocated filename of.
	 * @return Relocated filename of the given dependency.
	 */
	public @NotNull String getRelocatedFileName(@NotNull Dependency dependency) {
		return String.format(RELOCATED_FILENAME, dependency.getFileName().split("\\.jar")[0]);
	}

	/**
	 * Attempt to download and load internal dependencies.
	 *
	 * @param dependencies Dependencies to be downloaded and loaded.
	 * @throws DownloadFailureException if something went wrong while downloading the dependencies.
	 * @throws LoadFailureException     if something went wrong while loading the dependencies.
	 */
	private void loadInternalDependencies(List<Dependency> dependencies) throws DownloadFailureException, LoadFailureException {
		if (!dependenciesLoaded) {
			dragonfly.getDependencyDownloader().download(dependencies);
			dependencyLoader.load(dependencies);
			dependenciesLoaded = true;
		}
	}

	/**
	 * Get internal dependencies.
	 *
	 * @return Internal dependencies.
	 */
	public @NotNull List<Dependency> getDependencies() {
		return Arrays.asList(
				Dependency.maven(-3, "org.ow2.asm", "asm", "9.2"),
				Dependency.maven(-2, "org.ow2.asm", "asm-commons", "9.2"),
				Dependency.maven(-1, "me.lucko", "jar-relocator", "1.5")
		);
	}

}
```

## `src/main/java/dev/hypera/dragonfly/relocation/Relocation.java` (changed lines (35, 36, 38, 47, 51))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.relocation;

import org.jetbrains.annotations.NotNull;

/**
 * Relocation.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class Relocation {

	private final @NotNull String from;
	private final @NotNull String to;

	private Relocation(@NotNull String from, @NotNull String to) {
		this.from = from.replace("\\.", ".");
		this.to = to;
	}

	public static @NotNull Relocation of(@NotNull String from, @NotNull String to) {
		return new Relocation(from, to);
	}

	public @NotNull String getFrom() {
		return from;
	}

	public @NotNull String getTo() {
		return to;
	}

}
```

## `src/main/java/dev/hypera/dragonfly/resolvers/impl/MavenResolver.java` (changed lines (43,))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.resolvers.impl;

import dev.hypera.dragonfly.Dragonfly;
import dev.hypera.dragonfly.dependency.impl.MavenDependency;
import dev.hypera.dragonfly.exceptions.DownloadFailureException;
import dev.hypera.dragonfly.exceptions.ResolveFailureException;
import dev.hypera.dragonfly.resolvers.IResolver;
import java.util.Set;
import java.util.stream.Collectors;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

/**
 * Maven resolver.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class MavenResolver implements IResolver<MavenDependency> {

	private static final @NotNull String FORMAT = "%s%s/%s/%s/%s-%s.jar";

	@Override
	public @Nullable String resolve(@NotNull Dragonfly dragonfly, @NotNull MavenDependency dependency) throws ResolveFailureException {
		Set<String> urls = getUrls(dragonfly, dependency);
		if (urls.isEmpty()) {
			throw new ResolveFailureException("Cannot resolve dependency: " + dependency);
		}

		String resolvedUrl = null;
		for (String url : urls) {
			if (get(url, dragonfly.getTimeout()) != null) {
				resolvedUrl = url;
				break;
			}
		}

		if (null == resolvedUrl) {
			throw new ResolveFailureException("Cannot resolve dependency: " + dependency);
		}

		return resolvedUrl;
	}

	private Set<String> getUrls(@NotNull Dragonfly dragonfly, @NotNull MavenDependency dependency) {
		return dragonfly.getRepositories().stream().map(repo -> String.format(
				FORMAT, repo,
				dependency.getGroupId().replace(".", "/"),
				dependency.getArtifactId(),
				dependency.getVersion(),
				dependency.getArtifactId(),
				dependency.getVersion())
		).collect(Collectors.toSet());
	}

}
```

## `src/main/java/dev/hypera/dragonfly/resolvers/impl/MavenSnapshotResolver.java` (changed lines (30, 36, 42, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 96))

```java
/*
 * Dragonfly - Runtime dependency management library
 *  Copyright (c) 2021 Joshua Sing <joshua@hypera.dev>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

package dev.hypera.dragonfly.resolvers.impl;

import dev.hypera.dragonfly.Dragonfly;
import dev.hypera.dragonfly.dependency.impl.MavenDependency;
import dev.hypera.dragonfly.exceptions.ResolveFailureException;
import dev.hypera.dragonfly.resolvers.IResolver;
import java.io.IOException;
import java.io.StringReader;
import java.util.Set;
import java.util.stream.Collectors;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.xml.sax.InputSource;
import sun.tools.jstat.ParserException;

/**
 * Maven snapshot resolver.
 *
 * @author Joshua Sing <joshua@hypera.dev>
 */
public class MavenSnapshotResolver implements IResolver<MavenDependency> {

	private static final @NotNull String FORMAT = "%s%s/%s/%s/maven-metadata.xml";
	private static final @NotNull String OUTPUT_FORMAT = "%s/%s-%s-%s-%s.jar";
	private final @NotNull DocumentBuilderFactory documentBuilderFactory;

	public MavenSnapshotResolver() {
		try {
			/* The below is an attempt to create an XML parser while preventing XML External Entity attacks */
			/* Read more: https://cheatsheetseries.owasp.org/cheatsheets/XML_External_Entity_Prevention_Cheat_Sheet.html#java */
			DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
			factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
			factory.setFeature("http://xml.org/sax/features/external-general-entities", false);
			factory.setFeature("http://xml.org/sax/features/external-parameter-entities", false);
			factory.setFeature("http://apache.org/xml/features/nonvalidating/load-external-dtd", false);
			factory.setXIncludeAware(false);
			factory.setExpandEntityReferences(false);
			this.documentBuilderFactory = factory;
		} catch (ParserConfigurationException ex) {
			throw new RuntimeException("Failed to create DocumentBuilderFactory", ex);
		}
	}

	@Override
	public @Nullable String resolve(@NotNull Dragonfly dragonfly, @NotNull MavenDependency dependency) throws ResolveFailureException {
		if (!dependency.getVersion().contains("SNAPSHOT")) {
			throw new ResolveFailureException("Cannot resolve a dependency as a snapshot if it isn't a snapshot");
		} else {
			Set<String> urls = getUrls(dragonfly, dependency);
			if (urls.isEmpty()) {
				throw new ResolveFailureException("Cannot resolve dependency: " + dependency);
			}

			String data = null;
			String resolvedUrl = null;
			for (String url : urls) {
				if ((data = get(url, dragonfly.getTimeout())) != null) {
					resolvedUrl = url;
					break;
				}
			}

			if (null == data) {
				throw new ResolveFailureException("Cannot resolve dependency: " + dependency);
			}

			try {
				DocumentBuilder builder = documentBuilderFactory.newDocumentBuilder();
				Document document = builder.parse(new InputSource(new StringReader(data)));
				Element root = document.getDocumentElement();
				Element snapshotData = (Element) root.getElementsByTagName("snapshot").item(0);

				String timestamp = snapshotData.getElementsByTagName("timestamp").item(0).getTextContent();
				String buildNumber = snapshotData.getElementsByTagName("buildNumber").item(0).getTextContent();

				return String.format(
						OUTPUT_FORMAT,
						resolvedUrl.replace("/maven-metadata.xml", ""),
						dependency.getArtifactId(),
						dependency.getVersion().replace("-SNAPSHOT", ""),
						timestamp,
						buildNumber
				);
			} catch (Exception ex) {
				throw new ResolveFailureException("Cannot resolve dependency: " + dependency, ex);
			}
		}
	}


	private Set<String> getUrls(@NotNull Dragonfly dragonfly, @NotNull MavenDependency dependency) {
		return dragonfly.getRepositories().stream().map(repo -> String.format(
				FORMAT, repo,
				dependency.getGroupId().replace(".", "/"),
				dependency.getArtifactId(),
				dependency.getVersion()
		)).collect(Collectors.toSet());
	}

}
```
