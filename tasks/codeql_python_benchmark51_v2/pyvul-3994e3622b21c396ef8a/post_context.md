# Patch-overlapping Python context after the fix

## `rasa/model.py` — `function:unpack_model` (lines 210-239)

```python
def unpack_model(
    model_file: Text, working_directory: Optional[Union[Path, Text]] = None
) -> TempDirectoryPath:
    """Unpack a zipped Rasa model.

    Args:
        model_file: Path to zipped model.
        working_directory: Location where the model should be unpacked to.
                           If `None` a temporary directory will be created.

    Returns:
        Path to unpacked Rasa model.

    """
    import tarfile
    from tarsafe import TarSafe

    if working_directory is None:
        working_directory = tempfile.mkdtemp()

    # All files are in a subdirectory.
    try:
        with TarSafe.open(model_file, mode="r:gz") as tar:
            tar.extractall(working_directory)
            logger.debug(f"Extracted model to '{working_directory}'.")
    except (tarfile.TarError, ValueError) as e:
        logger.error(f"Failed to extract model at {model_file}. Error: {e}")
        raise

    return TempDirectoryPath(working_directory)
```

## `rasa/nlu/persistor.py` — `class:Persistor` (lines 50-107)

```python
class Persistor(abc.ABC):
    """Store models in cloud and fetch them when needed."""

    def persist(self, model_directory: Text, model_name: Text) -> None:
        """Uploads a model persisted in the `target_dir` to cloud storage."""
        if not os.path.isdir(model_directory):
            raise ValueError(f"Target directory '{model_directory}' not found.")

        file_key, tar_path = self._compress(model_directory, model_name)
        self._persist_tar(file_key, tar_path)

    def retrieve(self, model_name: Text, target_path: Text) -> None:
        """Downloads a model that has been persisted to cloud storage."""
        tar_name = model_name

        if not model_name.endswith("tar.gz"):
            # ensure backward compatibility
            tar_name = self._tar_name(model_name)

        self._retrieve_tar(tar_name)
        self._decompress(os.path.basename(tar_name), target_path)

    @abc.abstractmethod
    def _retrieve_tar(self, filename: Text) -> Text:
        """Downloads a model previously persisted to cloud storage."""
        raise NotImplementedError

    @abc.abstractmethod
    def _persist_tar(self, filekey: Text, tarname: Text) -> None:  # noqa: F841
        """Uploads a model persisted in the `target_dir` to cloud storage."""
        raise NotImplementedError

    def _compress(self, model_directory: Text, model_name: Text) -> Tuple[Text, Text]:
        """Creates a compressed archive and returns key and tar."""
        import tempfile

        dirpath = tempfile.mkdtemp()
        base_name = self._tar_name(model_name, include_extension=False)
        tar_name = shutil.make_archive(
            os.path.join(dirpath, base_name),
            "gztar",
            root_dir=model_directory,
            base_dir=".",
        )
        file_key = os.path.basename(tar_name)
        return file_key, tar_name

    @staticmethod
    def _tar_name(model_name: Text, include_extension: bool = True) -> Text:

        ext = ".tar.gz" if include_extension else ""
        return f"{model_name}{ext}"

    @staticmethod
    def _decompress(compressed_path: Text, target_path: Text) -> None:

        with TarSafe.open(compressed_path, "r:gz") as tar:
            tar.extractall(target_path)  # target dir will be created if it not exists
```

## `rasa/nlu/persistor.py` — `function:Persistor._decompress` (lines 103-107)

```python
    def _decompress(compressed_path: Text, target_path: Text) -> None:

        with TarSafe.open(compressed_path, "r:gz") as tar:
            tar.extractall(target_path)  # target dir will be created if it not exists
```

## `rasa/nlu/persistor.py` — `module:<module>@5` (lines 5-5)

```python
from tarsafe import TarSafe
```

## `rasa/utils/io.py` — `function:unarchive` (lines 85-99)

```python
def unarchive(byte_array: bytes, directory: Text) -> Text:
    """Tries to unpack a byte array interpreting it as an archive.

    Tries to use tar first to unpack, if that fails, zip will be used."""

    try:
        tar = TarSafe.open(fileobj=IOReader(byte_array))
        tar.extractall(directory)
        tar.close()
        return directory
    except tarfile.TarError:
        zip_ref = zipfile.ZipFile(IOReader(byte_array))
        zip_ref.extractall(directory)
        zip_ref.close()
        return directory
```

## `rasa/utils/io.py` — `module:<module>@6` (lines 6-6)

```python
from tarsafe import TarSafe
```
