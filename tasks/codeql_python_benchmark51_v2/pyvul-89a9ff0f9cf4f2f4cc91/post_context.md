# Patch-overlapping Python context after the fix

## `mangadex_downloader/cli/validator.py` — `function:_try_read` (lines 34-39)

```python
def _try_read(path):
    if not os.path.exists(path):
        return None
    
    with open(path, 'r') as o:
        return o.read()
```

## `mangadex_downloader/cli/validator.py` — `function:validate_url` (lines 41-49)

```python
def validate_url(url):
    urls = []
    for _url in url.splitlines():
        if not _url:
            continue

        urls.append((_validate(_url), _url))
    
    return urls
```

## `mangadex_downloader/cli/validator.py` — `function:validate` (lines 200-405)

```python
def validate(parser, args):
    urls = args.URL

    if (
        not args.search and 
        not args.fetch_library_manga and
        not args.fetch_library_list and
        not args.fetch_library_follows_list and
        not args.random and
        not args.fetch_group
    ):
        # Parsing file path
        if args.file:
            result = urls.split(':')
            file = result[1:]
            file_path = ""
            err_file = False

            try:
                file_path += file.pop(0)
            except IndexError:
                err_file = True
            
            if not file_path:
                err_file = True

            if err_file:
                parser.error("Syntax error: file path argument is empty")

            # Because ":" was removed during .split()
            # add it again
            for f in file:
                file_path += ':' + f

            # web URL location support for "file:{location}" syntax
            if file_path.startswith('http://') or file_path.startswith('https://'):
                r = Net.requests.get(file_path)
                try:
                    r.raise_for_status()
                except requests.HTTPError:
                    raise MangaDexException(f"Failed to connect '{file_path}', status code = {r.status_code}")

                file_path = r.text
            
            # Because this is specified syntax for batch downloading
            # If file doesn't exist, raise error
            elif not os.path.exists(file_path):
                parser.error(f"File \"{file_path}\" is not exist")
        else:
            file_content = _try_read(urls)
            file_path = file_content if file_content is not None else urls
        try:
            args.URL = validate_url(file_path)
        except argparse.ArgumentTypeError as e:
            parser.error(str(e))
        return

    kwargs = {
        'parser': parser
    }

    limit_items = None
    if args.random:
        def iter_random_manga():
            ids = []
            while True:
                _, raw_cr = get_key_value(urls, sep=":")
                content_ratings = split_comma_separated(raw_cr, single_value_to_list=True)
                if not content_ratings[0]:
                    # Fallback to default values
                    content_ratings = [i.value for i in ContentRating]
                else:
                    # Verify it
                    try:
                        content_ratings = [ContentRating(i).value for i in content_ratings]
                    except ValueError as e:
                        raise MangaDexException(e)

                params = {
                    'includes[]': ['author', 'artist', 'cover_art'],
                    "contentRating[]": content_ratings
                }
                r = Net.mangadex.get(f'{base_url}/manga/random', params=params)
                data = r.json()['data']
                manga = Manga(data=data)

                if manga.id not in ids:
                    # Make sure it's not duplicated manga
                    ids.append(manga.id)
                    yield manga

                continue

        iterator = iter_random_manga()
        text = f"Found random manga"
        on_empty_err = f"Unknown Error" # This should never happened
        on_preview = preview_cover_manga
        limit_items = 5
    elif args.fetch_group:
        # Getting group id
        _, group_id = get_key_value(urls, sep=':')
        if not group_id:
            parser.error("group id or url are required")
        
        group_id = __validate(group_id)
        group = Group(group_id)

        iterator = search(None, args.unsafe, group=group.id)
        text = f'List manga from group "{group.name}"'
        on_empty_err = f'Group "{group.name}" has no uploaded mangas'
        on_preview = preview_cover_manga
    elif args.search:
        filter_kwargs = {}
        filters = args.search_filter or []
        for f in filters:
            key, value  = get_key_value(f)
            try:
                value_filter_kwargs = filter_kwargs[key]
            except KeyError:
                filter_kwargs[key] = split_comma_separated(value)
            else:
                # Found duplicate filter with different value
                if isinstance(value_filter_kwargs, str):
                    new_values = [value_filter_kwargs]
                else:
                    new_values = value_filter_kwargs

                values = split_comma_separated(value, single_value_to_list=True)
                new_values.extend(values)

                filter_kwargs[key] = new_values

        iterator = search(urls, args.unsafe, **filter_kwargs)
        text = f"Search results for \"{urls}\""
        on_empty_err = f"Search results \"{urls}\" are empty"
        on_preview = preview_cover_manga
    elif args.fetch_library_manga:
        result = urls.split(':')
        
        # Try to get filter status
        try:
            status = result[1]
        except IndexError:
            status = None

        try:
            iterator = get_manga_from_user_library(status, args.unsafe)
        except MangaDexException as e:
            parser.error(str(e))
        
        user = Net.mangadex.user
        text = f"Manga library from user \"{user.name}\""
        on_empty_err = f"User \"{user.name}\" has no saved mangas"
        on_preview = preview_cover_manga
    elif args.fetch_library_list:
        # Try to get user (if available)
        _, user = get_key_value(urls, sep=':')

        user_id = None
        if user:
            try:
                user_id = __validate(user)
            except InvalidURL as e:
                parser.error(f"\"{user}\" is not a valid user")

        try:
            if user:
                iterator = get_list_from_user(user_id)
            else:
                iterator = get_list_from_user_library()
        except MangaDexException as e:
            parser.error(str(e))

        try:
            user = iterator.user
        except AttributeError:
            # Logged in user
            user = Net.mangadex.user

        text = f"MangaDex List library from user \"{user.name}\""
        on_empty_err = f"User \"{user.name}\" has no saved lists"
        on_preview = lambda x: preview_list(args, x)
    elif args.fetch_library_follows_list:
        try:
            iterator = get_followed_list_from_user_library()
        except MangaDexException as e:
            parser.error(str(e))
        
        user = Net.mangadex.user
        text = f"MangaDex followed List from user \"{user.name}\""
        on_empty_err = f"User \"{user.name}\" has no followed lists"
        on_preview = lambda x: preview_list(args, x)

    kwargs.update({
        'iterator': iterator,
        'text': text,
        'on_empty_err': on_empty_err,
        'on_preview': on_preview
    })

    if limit_items:
        kwargs.update(limit=limit_items)

    result = _create_prompt_choices(**kwargs)

    args.URL = validate_url(result.id)
```
