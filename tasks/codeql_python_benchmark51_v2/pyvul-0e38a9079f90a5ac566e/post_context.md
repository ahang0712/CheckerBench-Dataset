# Patch-overlapping Python context after the fix

## `src/MISP_maltego/transforms/attributetoevent.py` — `class:SearchInMISP` (lines 18-98)

```python
class SearchInMISP(Transform):
    """Search an attribute, event in MISP, allowing the use of % at the front and end"""
    input_type = Unknown
    display_name = 'Search in MISP'
    remote = True

    def do_transform(self, request, response, config):
        response += check_update(config)
        link_label = 'Search result'

        if 'properties.mispevent' in request.entity.fields:
            conn = MISPConnection(config, request.parameters)
            # if event_id
            try:
                if request.entity.value == '0':
                    return response
                eventid = int(request.entity.value)
                events_json = conn.misp.search(controller='events', eventid=eventid, with_attachments=False)
                for e in events_json:
                    response += event_to_entity(e, link_label=link_label, link_direction=LinkDirection.OutputToInput)
                return response
            except ValueError:
                pass
            # if event_info string as value
            events_json = conn.misp.search(controller='events', eventinfo=request.entity.value, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_label=link_label, link_direction=LinkDirection.OutputToInput)
            return response

        # From galaxy or Hashtag
        if 'properties.mispgalaxy' in request.entity.fields or 'properties.temp' in request.entity.fields:
            if request.entity.value == '-':
                return response
            # First search in galaxies
            keyword = get_entity_property(request.entity, 'Temp')
            if not keyword:
                keyword = request.entity.value
            # assume the user is searching for a cluster based on a substring.
            # Search in the list for those that match and return galaxy entities'
            potential_clusters = search_galaxy_cluster(keyword)
            # LATER check if duplicates are possible
            if potential_clusters:
                for potential_cluster in potential_clusters:
                    new_entity = galaxycluster_to_entity(potential_cluster, link_label=link_label)
                    # LATER support the type_filter - unfortunately this is not possible, we need Canari to tell us the original entity type
                    if isinstance(new_entity, MISPGalaxy):
                        response += new_entity

            # from Hashtag search also in tags
            if 'properties.temp' in request.entity.fields:
                keyword = get_entity_property(request.entity, 'Temp')
                if not keyword:
                    keyword = request.entity.value
                conn = MISPConnection(config, request.parameters)
                result = conn.misp.direct_call('tags/search', {'name': keyword})
                for t in result:
                    # skip misp-galaxies as we have processed them earlier on
                    if t['Tag']['name'].startswith('misp-galaxy'):
                        continue
                    # In this case we do not filter away those we add as notes, as people might want to pivot on it explicitly.
                    response += Hashtag(t['Tag']['name'], link_label=link_label, bookmark=Bookmark.Green)

            return response

        # for all other normal entities
        conn = MISPConnection(config, request.parameters)
        events_json = conn.misp.search(controller='events', value=request.entity.value, with_attachments=False)
        # we need to do really rebuild the Entity from scratch as request.entity is of type Unknown
        for e in events_json:
            # find the value as attribute
            attr = get_attribute_in_event(e, request.entity.value, substring=True)
            if attr:
                for item in attribute_to_entity(attr, only_self=True):
                    response += item
            # find the value as object, and return the object
            if 'Object' in e['Event']:
                for o in e['Event']['Object']:
                    if get_attribute_in_object(o, attribute_value=request.entity.value, substring=True).get('value'):
                        response += conn.object_to_entity(o, link_label=link_label)

        return response
```

## `src/MISP_maltego/transforms/attributetoevent.py` — `function:SearchInMISP.do_transform` (lines 24-98)

```python
    def do_transform(self, request, response, config):
        response += check_update(config)
        link_label = 'Search result'

        if 'properties.mispevent' in request.entity.fields:
            conn = MISPConnection(config, request.parameters)
            # if event_id
            try:
                if request.entity.value == '0':
                    return response
                eventid = int(request.entity.value)
                events_json = conn.misp.search(controller='events', eventid=eventid, with_attachments=False)
                for e in events_json:
                    response += event_to_entity(e, link_label=link_label, link_direction=LinkDirection.OutputToInput)
                return response
            except ValueError:
                pass
            # if event_info string as value
            events_json = conn.misp.search(controller='events', eventinfo=request.entity.value, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_label=link_label, link_direction=LinkDirection.OutputToInput)
            return response

        # From galaxy or Hashtag
        if 'properties.mispgalaxy' in request.entity.fields or 'properties.temp' in request.entity.fields:
            if request.entity.value == '-':
                return response
            # First search in galaxies
            keyword = get_entity_property(request.entity, 'Temp')
            if not keyword:
                keyword = request.entity.value
            # assume the user is searching for a cluster based on a substring.
            # Search in the list for those that match and return galaxy entities'
            potential_clusters = search_galaxy_cluster(keyword)
            # LATER check if duplicates are possible
            if potential_clusters:
                for potential_cluster in potential_clusters:
                    new_entity = galaxycluster_to_entity(potential_cluster, link_label=link_label)
                    # LATER support the type_filter - unfortunately this is not possible, we need Canari to tell us the original entity type
                    if isinstance(new_entity, MISPGalaxy):
                        response += new_entity

            # from Hashtag search also in tags
            if 'properties.temp' in request.entity.fields:
                keyword = get_entity_property(request.entity, 'Temp')
                if not keyword:
                    keyword = request.entity.value
                conn = MISPConnection(config, request.parameters)
                result = conn.misp.direct_call('tags/search', {'name': keyword})
                for t in result:
                    # skip misp-galaxies as we have processed them earlier on
                    if t['Tag']['name'].startswith('misp-galaxy'):
                        continue
                    # In this case we do not filter away those we add as notes, as people might want to pivot on it explicitly.
                    response += Hashtag(t['Tag']['name'], link_label=link_label, bookmark=Bookmark.Green)

            return response

        # for all other normal entities
        conn = MISPConnection(config, request.parameters)
        events_json = conn.misp.search(controller='events', value=request.entity.value, with_attachments=False)
        # we need to do really rebuild the Entity from scratch as request.entity is of type Unknown
        for e in events_json:
            # find the value as attribute
            attr = get_attribute_in_event(e, request.entity.value, substring=True)
            if attr:
                for item in attribute_to_entity(attr, only_self=True):
                    response += item
            # find the value as object, and return the object
            if 'Object' in e['Event']:
                for o in e['Event']['Object']:
                    if get_attribute_in_object(o, attribute_value=request.entity.value, substring=True).get('value'):
                        response += conn.object_to_entity(o, link_label=link_label)

        return response
```

## `src/MISP_maltego/transforms/attributetoevent.py` — `class:AttributeToEvent` (lines 123-184)

```python
class AttributeToEvent(Transform):
    input_type = Unknown
    display_name = 'to MISP Event'
    remote = True

    def do_transform(self, request, response, config):
        response += check_update(config)
        # skip some Entities
        skip = ['properties.mispevent']
        for i in skip:
            if i in request.entity.fields:
                return response

        if 'ipv4-range' in request.entity.fields:
            # placeholder for https://github.com/MISP/MISP-maltego/issues/11
            pass

        conn = MISPConnection(config, request.parameters)
        # from Galaxy
        if 'properties.mispgalaxy' in request.entity.fields:
            tag_name = get_entity_property(request.entity, 'tag_name')
            if not tag_name:
                tag_name = request.entity.value
            events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            return response
        # from Object
        elif 'properties.mispobject' in request.entity.fields:
            if request.entity.fields.get('event_id'):
                events_json = conn.misp.search(controller='events', eventid=request.entity.fields.get('event_id').value, with_attachments=False)
                for e in events_json:
                    response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
                return response
            else:
                return response
        # from Hashtag
        elif 'properties.temp' in request.entity.fields:
            tag_name = get_entity_property(request.entity, 'Temp')
            if not tag_name:
                tag_name = request.entity.value
            events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            return response
        # standard Entities (normal attributes)
        else:
            events_json = conn.misp.search(controller='events', value=request.entity.value, with_attachments=False)

        # return the MISPEvent or MISPObject of the attribute
        for e in events_json:
            # find the value as attribute
            attr = get_attribute_in_event(e, request.entity.value)
            if attr:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            # find the value as object
            if 'Object' in e['Event']:
                for o in e['Event']['Object']:
                    if get_attribute_in_object(o, attribute_value=request.entity.value).get('value'):
                        response += conn.object_to_entity(o, link_direction=LinkDirection.OutputToInput)

        return response
```

## `src/MISP_maltego/transforms/attributetoevent.py` — `function:AttributeToEvent.do_transform` (lines 128-184)

```python
    def do_transform(self, request, response, config):
        response += check_update(config)
        # skip some Entities
        skip = ['properties.mispevent']
        for i in skip:
            if i in request.entity.fields:
                return response

        if 'ipv4-range' in request.entity.fields:
            # placeholder for https://github.com/MISP/MISP-maltego/issues/11
            pass

        conn = MISPConnection(config, request.parameters)
        # from Galaxy
        if 'properties.mispgalaxy' in request.entity.fields:
            tag_name = get_entity_property(request.entity, 'tag_name')
            if not tag_name:
                tag_name = request.entity.value
            events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            return response
        # from Object
        elif 'properties.mispobject' in request.entity.fields:
            if request.entity.fields.get('event_id'):
                events_json = conn.misp.search(controller='events', eventid=request.entity.fields.get('event_id').value, with_attachments=False)
                for e in events_json:
                    response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
                return response
            else:
                return response
        # from Hashtag
        elif 'properties.temp' in request.entity.fields:
            tag_name = get_entity_property(request.entity, 'Temp')
            if not tag_name:
                tag_name = request.entity.value
            events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
            for e in events_json:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            return response
        # standard Entities (normal attributes)
        else:
            events_json = conn.misp.search(controller='events', value=request.entity.value, with_attachments=False)

        # return the MISPEvent or MISPObject of the attribute
        for e in events_json:
            # find the value as attribute
            attr = get_attribute_in_event(e, request.entity.value)
            if attr:
                response += event_to_entity(e, link_direction=LinkDirection.OutputToInput)
            # find the value as object
            if 'Object' in e['Event']:
                for o in e['Event']['Object']:
                    if get_attribute_in_object(o, attribute_value=request.entity.value).get('value'):
                        response += conn.object_to_entity(o, link_direction=LinkDirection.OutputToInput)

        return response
```

## `src/MISP_maltego/transforms/attributetoevent.py` — `module:<module>@4` (lines 4-4)

```python
from MISP_maltego.transforms.common.util import check_update, MISPConnection, event_to_entity, get_attribute_in_event, get_attribute_in_object, attribute_to_entity, get_entity_property, search_galaxy_cluster, galaxycluster_to_entity
```

## `src/MISP_maltego/transforms/common/util.py` — `class:MISPConnection` (lines 66-175)

```python
class MISPConnection():
    def __init__(self, config=None, parameters=None):
        self.misp = None

        if not config:
            raise MaltegoException("ERROR: MISP connection not yet established, and config not provided as parameter.")
        misp_verify = True
        misp_debug = False
        misp_url = None
        misp_key = None
        try:
            if is_local_exec_mode():
                misp_url = config['MISP_maltego.local.misp_url']
                misp_key = config['MISP_maltego.local.misp_key']
                if config['MISP_maltego.local.misp_verify'] in ['False', 'false', 0, 'no', 'No']:
                    misp_verify = False
                if config['MISP_maltego.local.misp_debug'] in ['True', 'true', 1, 'yes', 'Yes']:
                    misp_debug = True
            else:
                try:
                    misp_url = parameters['mispurl'].value
                    misp_key = parameters['mispkey'].value
                except AttributeError:
                    raise MaltegoException("ERROR: mispurl and mispkey need to be set to something valid")
            self.misp = PyMISP(misp_url, misp_key, misp_verify, 'json', misp_debug, tool='misp_maltego')
        except Exception:
            if is_local_exec_mode():
                raise MaltegoException("ERROR: Cannot connect to MISP server. Please verify your MISP_Maltego.conf settings.")
            else:
                raise MaltegoException("ERROR: Cannot connect to MISP server. Please verify your settings (MISP URL and API key), and ensure the MISP server is reachable from the internet.")

    def object_to_entity(self, o, link_label=None, link_direction=LinkDirection.InputToOutput):
        # find a nice icon for it
        try:
            icon_url = mapping_object_icon[o['name']]
        except KeyError:
            # it's not in our mapping, just ignore and leave the default icon
            icon_url = None
        # Generate a human readable display-name:
        # - find the first RequiredOneOf that exists
        # - if none, use the first RequiredField
        # LATER further finetune the human readable version of this object
        o_template = self.misp.get_object_template(o['template_uuid'])
        human_readable = None
        try:
            found = False
            while not found:  # the while loop is broken once something is found, or the requiredOneOf has no elements left
                required_ote_type = o_template['ObjectTemplate']['requirements']['requiredOneOf'].pop(0)
                for ote in o_template['ObjectTemplateElement']:
                    if ote['object_relation'] == required_ote_type:
                        required_a_type = ote['type']
                        break
                for a in o['Attribute']:
                    if a['type'] == required_a_type:
                        human_readable = '{}:\n{}'.format(o['name'], a['value'])
                        found = True
                        break
        except Exception:
            pass
        if not human_readable:
            try:
                found = False
                parts = []
                for required_ote_type in o_template['ObjectTemplate']['requirements']['required']:
                    for ote in o_template['ObjectTemplateElement']:
                        if ote['object_relation'] == required_ote_type:
                            required_a_type = ote['type']
                            break
                    for a in o['Attribute']:
                        if a['type'] == required_a_type:
                            parts.append(a['value'])
                            break
                human_readable = '{}:\n{}'.format(o['name'], '|'.join(parts))
            except Exception:
                human_readable = o['name']
        return MISPObject(
            human_readable,
            uuid=o['uuid'],
            event_id=int(o['event_id']),
            meta_category=o.get('meta_category'),
            description=o.get('description'),
            comment=o.get('comment'),
            icon_url=icon_url,
            link_label=link_label,
            link_direction=link_direction,
            bookmark=Bookmark.Green
        )

    def object_to_relations(self, o, e):
        # process forward and reverse references, so just loop over all the objects of the event
        if 'Object' in e['Event']:
            for eo in e['Event']['Object']:
                if 'ObjectReference' in eo:
                    for ref in eo['ObjectReference']:
                        # we have found original object. Expand to the related object and attributes
                        if eo['uuid'] == o['uuid']:
                            # the reference is an Object
                            if ref.get('Object'):
                                # get the full object in the event, as our objectReference included does not contain everything we need
                                sub_object = get_object_in_event(ref['Object']['uuid'], e)
                                yield self.object_to_entity(sub_object, link_label=ref['relationship_type'])
                            # the reference is an Attribute
                            if ref.get('Attribute'):
                                ref['Attribute']['event_id'] = ref['event_id']   # LATER remove this ugly workaround - object can't be requested directly from MISP using the uuid, and to find a full object we need the event_id
                                for item in attribute_to_entity(ref['Attribute'], link_label=ref['relationship_type']):
                                    yield item

                        # reverse-lookup - this is another objects relating the original object
                        if ref['referenced_uuid'] == o['uuid']:
                            yield self.object_to_entity(eo, link_label=ref['relationship_type'], link_direction=LinkDirection.OutputToInput)
```

## `src/MISP_maltego/transforms/common/util.py` — `function:MISPConnection.__init__` (lines 67-95)

```python
    def __init__(self, config=None, parameters=None):
        self.misp = None

        if not config:
            raise MaltegoException("ERROR: MISP connection not yet established, and config not provided as parameter.")
        misp_verify = True
        misp_debug = False
        misp_url = None
        misp_key = None
        try:
            if is_local_exec_mode():
                misp_url = config['MISP_maltego.local.misp_url']
                misp_key = config['MISP_maltego.local.misp_key']
                if config['MISP_maltego.local.misp_verify'] in ['False', 'false', 0, 'no', 'No']:
                    misp_verify = False
                if config['MISP_maltego.local.misp_debug'] in ['True', 'true', 1, 'yes', 'Yes']:
                    misp_debug = True
            else:
                try:
                    misp_url = parameters['mispurl'].value
                    misp_key = parameters['mispkey'].value
                except AttributeError:
                    raise MaltegoException("ERROR: mispurl and mispkey need to be set to something valid")
            self.misp = PyMISP(misp_url, misp_key, misp_verify, 'json', misp_debug, tool='misp_maltego')
        except Exception:
            if is_local_exec_mode():
                raise MaltegoException("ERROR: Cannot connect to MISP server. Please verify your MISP_Maltego.conf settings.")
            else:
                raise MaltegoException("ERROR: Cannot connect to MISP server. Please verify your settings (MISP URL and API key), and ensure the MISP server is reachable from the internet.")
```

## `src/MISP_maltego/transforms/common/util.py` — `function:MISPConnection.object_to_entity` (lines 97-152)

```python
    def object_to_entity(self, o, link_label=None, link_direction=LinkDirection.InputToOutput):
        # find a nice icon for it
        try:
            icon_url = mapping_object_icon[o['name']]
        except KeyError:
            # it's not in our mapping, just ignore and leave the default icon
            icon_url = None
        # Generate a human readable display-name:
        # - find the first RequiredOneOf that exists
        # - if none, use the first RequiredField
        # LATER further finetune the human readable version of this object
        o_template = self.misp.get_object_template(o['template_uuid'])
        human_readable = None
        try:
            found = False
            while not found:  # the while loop is broken once something is found, or the requiredOneOf has no elements left
                required_ote_type = o_template['ObjectTemplate']['requirements']['requiredOneOf'].pop(0)
                for ote in o_template['ObjectTemplateElement']:
                    if ote['object_relation'] == required_ote_type:
                        required_a_type = ote['type']
                        break
                for a in o['Attribute']:
                    if a['type'] == required_a_type:
                        human_readable = '{}:\n{}'.format(o['name'], a['value'])
                        found = True
                        break
        except Exception:
            pass
        if not human_readable:
            try:
                found = False
                parts = []
                for required_ote_type in o_template['ObjectTemplate']['requirements']['required']:
                    for ote in o_template['ObjectTemplateElement']:
                        if ote['object_relation'] == required_ote_type:
                            required_a_type = ote['type']
                            break
                    for a in o['Attribute']:
                        if a['type'] == required_a_type:
                            parts.append(a['value'])
                            break
                human_readable = '{}:\n{}'.format(o['name'], '|'.join(parts))
            except Exception:
                human_readable = o['name']
        return MISPObject(
            human_readable,
            uuid=o['uuid'],
            event_id=int(o['event_id']),
            meta_category=o.get('meta_category'),
            description=o.get('description'),
            comment=o.get('comment'),
            icon_url=icon_url,
            link_label=link_label,
            link_direction=link_direction,
            bookmark=Bookmark.Green
        )
```

## `src/MISP_maltego/transforms/common/util.py` — `function:MISPConnection.object_to_relations` (lines 154-175)

```python
    def object_to_relations(self, o, e):
        # process forward and reverse references, so just loop over all the objects of the event
        if 'Object' in e['Event']:
            for eo in e['Event']['Object']:
                if 'ObjectReference' in eo:
                    for ref in eo['ObjectReference']:
                        # we have found original object. Expand to the related object and attributes
                        if eo['uuid'] == o['uuid']:
                            # the reference is an Object
                            if ref.get('Object'):
                                # get the full object in the event, as our objectReference included does not contain everything we need
                                sub_object = get_object_in_event(ref['Object']['uuid'], e)
                                yield self.object_to_entity(sub_object, link_label=ref['relationship_type'])
                            # the reference is an Attribute
                            if ref.get('Attribute'):
                                ref['Attribute']['event_id'] = ref['event_id']   # LATER remove this ugly workaround - object can't be requested directly from MISP using the uuid, and to find a full object we need the event_id
                                for item in attribute_to_entity(ref['Attribute'], link_label=ref['relationship_type']):
                                    yield item

                        # reverse-lookup - this is another objects relating the original object
                        if ref['referenced_uuid'] == o['uuid']:
                            yield self.object_to_entity(eo, link_label=ref['relationship_type'], link_direction=LinkDirection.OutputToInput)
```

## `src/MISP_maltego/transforms/common/util.py` — `function:object_to_attributes` (lines 257-267)

```python
def object_to_attributes(o, e):
    # first process attributes from an object that belong together (eg: first-name + last-name), and remove them from the list
    if o['name'] == 'person':
        first_name = get_attribute_in_object(o, attribute_type='first-name', drop=True).get('value')
        last_name = get_attribute_in_object(o, attribute_type='last-name', drop=True).get('value')
        yield entity_obj_to_entity(Person, ' '.join([first_name, last_name]).strip(), 'person', lastname=last_name, firstnames=first_name, bookmark=Bookmark.Green)

    # process normal attributes
    for a in o['Attribute']:
        for item in attribute_to_entity(a):
            yield item
```

## `src/MISP_maltego/transforms/common/util.py` — `function:get_object_in_event` (lines 270-273)

```python
def get_object_in_event(uuid, e):
    for o in e['Event']['Object']:
        if o['uuid'] == uuid:
            return o
```

## `src/MISP_maltego/transforms/common/util.py` — `module:<module>@21` (lines 21-21)

```python
update_url = 'https://raw.githubusercontent.com/MISP/MISP-maltego/master/setup.py'
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `class:EventToTransform` (lines 21-83)

```python
class EventToTransform(Transform):
    input_type = None
    """Generic EventTo class containing multiple reusable functions for the subclasses."""

    def __init__(self):
        self.request = None
        self.response = None
        self.config = None
        self.conn = None
        self.event_json = None
        self.event_tags = None

    def do_transform(self, request, response, config):
        self.request = request
        self.response = response
        self.config = config
        self.response += check_update(config)
        maltego_misp_event = request.entity
        self.conn = MISPConnection(config, request.parameters)
        event_id = maltego_misp_event.id
        search_result = self.conn.misp.search(controller='events', eventid=event_id, with_attachments=False)
        if search_result:
            self.event_json = search_result.pop()
        else:
            return False

        self.response += event_to_entity(self.event_json)
        return True

    def gen_response_tags(self, gen_response=True):
        self.event_tags = []
        if 'Tag' in self.event_json['Event']:
            for t in self.event_json['Event']['Tag']:
                self.event_tags.append(t['name'])
                # ignore all misp-galaxies
                if t['name'].startswith('misp-galaxy'):
                    continue
                # ignore all those we add as notes
                if tag_matches_note_prefix(t['name']):
                    continue
                if gen_response:
                    self.response += Hashtag(t['name'])

    def gen_response_galaxies(self):
        for g in self.event_json['Event']['Galaxy']:
            for c in g['GalaxyCluster']:
                self.response += galaxycluster_to_entity(c)

    def gen_response_attributes(self):
        if not self.event_tags:
            self.gen_response_tags(gen_response=False)
        for a in self.event_json['Event']["Attribute"]:
            for entity in attribute_to_entity(a, event_tags=self.event_tags):
                if entity:
                    self.response += entity

    def gen_response_objects(self):
        for o in self.event_json['Event']['Object']:
            self.response += self.conn.object_to_entity(o)

    def gen_response_relations(self):
        for e in self.event_json['Event']['RelatedEvent']:
            self.response += event_to_entity(e, link_style=LinkStyle.DashDot)
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `function:EventToTransform.__init__` (lines 25-31)

```python
    def __init__(self):
        self.request = None
        self.response = None
        self.config = None
        self.conn = None
        self.event_json = None
        self.event_tags = None
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `function:EventToTransform.do_transform` (lines 33-48)

```python
    def do_transform(self, request, response, config):
        self.request = request
        self.response = response
        self.config = config
        self.response += check_update(config)
        maltego_misp_event = request.entity
        self.conn = MISPConnection(config, request.parameters)
        event_id = maltego_misp_event.id
        search_result = self.conn.misp.search(controller='events', eventid=event_id, with_attachments=False)
        if search_result:
            self.event_json = search_result.pop()
        else:
            return False

        self.response += event_to_entity(self.event_json)
        return True
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `function:EventToTransform.gen_response_objects` (lines 77-79)

```python
    def gen_response_objects(self):
        for o in self.event_json['Event']['Object']:
            self.response += self.conn.object_to_entity(o)
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `class:ObjectToAttributes` (lines 163-183)

```python
class ObjectToAttributes(Transform):
    """"Expands an object to its attributes"""
    input_type = MISPObject
    description = 'Expands an Object to Attributes'
    remote = True

    def do_transform(self, request, response, config):
        response += check_update(config)
        maltego_object = request.entity
        conn = MISPConnection(config, request.parameters)
        event_json = conn.misp.get_event(maltego_object.event_id)
        for o in event_json['Event']['Object']:
            if o['uuid'] == maltego_object.uuid:
                for entity in object_to_attributes(o, event_json):
                    if entity:
                        response += entity
                for entity in conn.object_to_relations(o, event_json):
                    if entity:
                        response += entity

        return response
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `function:ObjectToAttributes.do_transform` (lines 169-183)

```python
    def do_transform(self, request, response, config):
        response += check_update(config)
        maltego_object = request.entity
        conn = MISPConnection(config, request.parameters)
        event_json = conn.misp.get_event(maltego_object.event_id)
        for o in event_json['Event']['Object']:
            if o['uuid'] == maltego_object.uuid:
                for entity in object_to_attributes(o, event_json):
                    if entity:
                        response += entity
                for entity in conn.object_to_relations(o, event_json):
                    if entity:
                        response += entity

        return response
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `class:ObjectToRelations` (lines 186-203)

```python
class ObjectToRelations(Transform):
    """Expands an object to the relations of the object"""
    input_type = MISPObject
    description = 'Expands an Object to Relations'
    remote = True

    def do_transform(self, request, response, config):
        response += check_update(config)
        maltego_object = request.entity
        conn = MISPConnection(config, request.parameters)
        event_json = conn.misp.get_event(maltego_object.event_id)
        for o in event_json['Event']['Object']:
            if o['uuid'] == maltego_object.uuid:
                for entity in conn.object_to_relations(o, event_json):
                    if entity:
                        response += entity

        return response
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `function:ObjectToRelations.do_transform` (lines 192-203)

```python
    def do_transform(self, request, response, config):
        response += check_update(config)
        maltego_object = request.entity
        conn = MISPConnection(config, request.parameters)
        event_json = conn.misp.get_event(maltego_object.event_id)
        for o in event_json['Event']['Object']:
            if o['uuid'] == maltego_object.uuid:
                for entity in conn.object_to_relations(o, event_json):
                    if entity:
                        response += entity

        return response
```

## `src/MISP_maltego/transforms/eventtoattributes.py` — `module:<module>@4` (lines 4-4)

```python
from MISP_maltego.transforms.common.util import check_update, MISPConnection, attribute_to_entity, event_to_entity, galaxycluster_to_entity, object_to_attributes, tag_matches_note_prefix
```

## `src/MISP_maltego/transforms/galaxytoevent.py` — `class:GalaxyToEvents` (lines 18-35)

```python
class GalaxyToEvents(Transform):
    """Expands a Galaxy to multiple MISP Events."""

    # The transform input entity type.
    input_type = MISPGalaxy
    remote = True

    def do_transform(self, request, response, config):
        response += check_update(config)
        conn = MISPConnection(config, request.parameters)
        if request.entity.tag_name:
            tag_name = request.entity.tag_name
        else:
            tag_name = request.entity.value
        events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
        for e in events_json:
            response += MISPEvent(e['Event']['id'], uuid=e['Event']['uuid'], info=e['Event']['info'], link_direction=LinkDirection.OutputToInput)
        return response
```

## `src/MISP_maltego/transforms/galaxytoevent.py` — `function:GalaxyToEvents.do_transform` (lines 25-35)

```python
    def do_transform(self, request, response, config):
        response += check_update(config)
        conn = MISPConnection(config, request.parameters)
        if request.entity.tag_name:
            tag_name = request.entity.tag_name
        else:
            tag_name = request.entity.value
        events_json = conn.misp.search(controller='events', tags=tag_name, with_attachments=False)
        for e in events_json:
            response += MISPEvent(e['Event']['id'], uuid=e['Event']['uuid'], info=e['Event']['info'], link_direction=LinkDirection.OutputToInput)
        return response
```

## `src/MISP_maltego/transforms/galaxytoevent.py` — `module:<module>@3` (lines 3-3)

```python
from MISP_maltego.transforms.common.util import check_update, MISPConnection, galaxycluster_to_entity, get_galaxy_cluster, get_galaxies_relating, search_galaxy_cluster, mapping_galaxy_icon
```
