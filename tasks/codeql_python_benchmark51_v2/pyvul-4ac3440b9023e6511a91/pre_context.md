# Patch-overlapping Python context before the fix

## `nautobot_device_onboarding/jobs.py` — `class:OnboardingTask` (lines 15-86)

```python
class OnboardingTask(Job):
    class Meta:
        """Meta object boilerplate for onboarding."""

        name = "Perform Device Onboarding"
        description = "Login to a device and populate Nautobot device object."
        has_sensitive_variables = False

    location = ObjectVar(model=Location, query_params={"": ""}, required=False, description="")
    ip_address = IPAddressVar(description="", label="")
    port = IntegerVar(description="")
    timeout = IntegerVar(description="")
    credentials = ObjectVar(model=SecretsGroup, query_params={"": ""}, required=False, description="")
    platform = ObjectVar(model=Platform, required=False, description="")
    role = ObjectVar(model=Role, query_params={"": ""}, required=False, description="")
    device_type = ObjectVar(model=DeviceType, required=False, description="")

    def run(self, *args, **data):
        """Process a single Onboarding Task instance."""
        self.logger.info("START: onboard device")
        credentials = self._parse_credentials(data["credentials"])
        platform = data["platform"]
        netdev = NetdevKeeper(
            hostname=data["ip_address"],
            port=data["port"],
            timeout=data["timeout"],
            username=credentials["username"],
            password=credentials["password"],
            secret=credentials["secret"],
            napalm_driver=platform.napalm_driver if platform and platform.napalm_driver else None,
            optional_args=platform.napalm_args if platform and platform.napalm_args else settings.NAPALM_ARGS,
        )
        netdev.get_onboarding_facts()
        netdev_dict = netdev.get_netdev_dict()

        onboarding_kwargs = {
            # Kwargs extracted from OnboardingTask:
            "netdev_mgmt_ip_address": data["ip_address"],
            "netdev_nb_location_name": data["location"].name,
            "netdev_nb_device_type_name": data["device_type"],
            "netdev_nb_role_name": data["role"].name if data["role"] else PLUGIN_SETTINGS["default_device_role"],
            "netdev_nb_role_color": PLUGIN_SETTINGS["default_device_role_color"],
            "netdev_nb_platform_name": data["platform"].name if data["platform"] else None,
            # Kwargs discovered on the Onboarded Device:
            "netdev_hostname": netdev_dict["netdev_hostname"],
            "netdev_vendor": netdev_dict["netdev_vendor"],
            "netdev_model": netdev_dict["netdev_model"],
            "netdev_serial_number": netdev_dict["netdev_serial_number"],
            "netdev_mgmt_ifname": netdev_dict["netdev_mgmt_ifname"],
            "netdev_mgmt_pflen": netdev_dict["netdev_mgmt_pflen"],
            "netdev_netmiko_device_type": netdev_dict["netdev_netmiko_device_type"],
            "onboarding_class": netdev_dict["onboarding_class"],
            "driver_addon_result": netdev_dict["driver_addon_result"],
        }
        onboarding_cls = netdev_dict["onboarding_class"]()
        onboarding_cls.credentials = {"username": self.username, "password": self.password, "secret": self.secret}
        onboarding_cls.run(onboarding_kwargs=onboarding_kwargs)

    def _parse_credentials(self, credentials):
        """Parse and return dictionary of credentials."""

        if credentials:
            self.username = (credentials.secrets.get(secret_type=SecretsGroupSecretTypeChoices.TYPE_USERNAME),)
            self.password = (credentials.secrets.get(secret_type=SecretsGroupSecretTypeChoices.TYPE_PASSWORD),)
            self.secret = (None,)
            secret = credentials.secrets.filter(secret_type=SecretsGroupSecretTypeChoices.TYPE_SECRET)
            if secret.exists():
                self.secret = secret.first()
        else:
            self.username = (settings.NAPALM_USERNAME,)
            self.password = (settings.NAPALM_PASSWORD,)
            self.secret = (settings.NAPALM_ARGS.get("secret", None),)
```

## `nautobot_device_onboarding/jobs.py` — `class:OnboardingTask.Meta` (lines 16-21)

```python
    class Meta:
        """Meta object boilerplate for onboarding."""

        name = "Perform Device Onboarding"
        description = "Login to a device and populate Nautobot device object."
        has_sensitive_variables = False
```

## `nautobot_device_onboarding/jobs.py` — `function:OnboardingTask.run` (lines 32-71)

```python
    def run(self, *args, **data):
        """Process a single Onboarding Task instance."""
        self.logger.info("START: onboard device")
        credentials = self._parse_credentials(data["credentials"])
        platform = data["platform"]
        netdev = NetdevKeeper(
            hostname=data["ip_address"],
            port=data["port"],
            timeout=data["timeout"],
            username=credentials["username"],
            password=credentials["password"],
            secret=credentials["secret"],
            napalm_driver=platform.napalm_driver if platform and platform.napalm_driver else None,
            optional_args=platform.napalm_args if platform and platform.napalm_args else settings.NAPALM_ARGS,
        )
        netdev.get_onboarding_facts()
        netdev_dict = netdev.get_netdev_dict()

        onboarding_kwargs = {
            # Kwargs extracted from OnboardingTask:
            "netdev_mgmt_ip_address": data["ip_address"],
            "netdev_nb_location_name": data["location"].name,
            "netdev_nb_device_type_name": data["device_type"],
            "netdev_nb_role_name": data["role"].name if data["role"] else PLUGIN_SETTINGS["default_device_role"],
            "netdev_nb_role_color": PLUGIN_SETTINGS["default_device_role_color"],
            "netdev_nb_platform_name": data["platform"].name if data["platform"] else None,
            # Kwargs discovered on the Onboarded Device:
            "netdev_hostname": netdev_dict["netdev_hostname"],
            "netdev_vendor": netdev_dict["netdev_vendor"],
            "netdev_model": netdev_dict["netdev_model"],
            "netdev_serial_number": netdev_dict["netdev_serial_number"],
            "netdev_mgmt_ifname": netdev_dict["netdev_mgmt_ifname"],
            "netdev_mgmt_pflen": netdev_dict["netdev_mgmt_pflen"],
            "netdev_netmiko_device_type": netdev_dict["netdev_netmiko_device_type"],
            "onboarding_class": netdev_dict["onboarding_class"],
            "driver_addon_result": netdev_dict["driver_addon_result"],
        }
        onboarding_cls = netdev_dict["onboarding_class"]()
        onboarding_cls.credentials = {"username": self.username, "password": self.password, "secret": self.secret}
        onboarding_cls.run(onboarding_kwargs=onboarding_kwargs)
```

## `nautobot_device_onboarding/jobs.py` — `function:OnboardingTask._parse_credentials` (lines 73-86)

```python
    def _parse_credentials(self, credentials):
        """Parse and return dictionary of credentials."""

        if credentials:
            self.username = (credentials.secrets.get(secret_type=SecretsGroupSecretTypeChoices.TYPE_USERNAME),)
            self.password = (credentials.secrets.get(secret_type=SecretsGroupSecretTypeChoices.TYPE_PASSWORD),)
            self.secret = (None,)
            secret = credentials.secrets.filter(secret_type=SecretsGroupSecretTypeChoices.TYPE_SECRET)
            if secret.exists():
                self.secret = secret.first()
        else:
            self.username = (settings.NAPALM_USERNAME,)
            self.password = (settings.NAPALM_PASSWORD,)
            self.secret = (settings.NAPALM_ARGS.get("secret", None),)
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `class:NetdevKeeperTestCase` (lines 17-29)

```python
class NetdevKeeperTestCase(TestCase):
    """Test the NetdevKeeper Class."""

    def setUp(self):
        """Create a superuser and token for API calls."""
        role_content_type = ContentType.objects.get_for_model(Device)
        status = Status.objects.get(name="Active")
        location_type = LocationType.objects.create(name="site")
        self.site1 = Location.objects.create(name="USWEST", location_type=location_type, status=status)
        self.device_role1 = Role.objects.create(name="Firewall")
        self.device_role1.content_types.set([role_content_type])

        self.platform1 = Platform.objects.create(name="JunOS", napalm_driver="junos")
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `function:NetdevKeeperTestCase.setUp` (lines 20-29)

```python
    def setUp(self):
        """Create a superuser and token for API calls."""
        role_content_type = ContentType.objects.get_for_model(Device)
        status = Status.objects.get(name="Active")
        location_type = LocationType.objects.create(name="site")
        self.site1 = Location.objects.create(name="USWEST", location_type=location_type, status=status)
        self.device_role1 = Role.objects.create(name="Firewall")
        self.device_role1.content_types.set([role_content_type])

        self.platform1 = Platform.objects.create(name="JunOS", napalm_driver="junos")
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@1` (lines 1-1)

```python
"""Unit tests for nautobot_device_onboarding.netdev_keeper module and its classes."""
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@3` (lines 3-3)

```python
from socket import gaierror
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@4` (lines 4-4)

```python
from unittest import mock
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@6` (lines 6-6)

```python
from django.contrib.contenttypes.models import ContentType
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@7` (lines 7-7)

```python
from django.test import TestCase
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@9` (lines 9-9)

```python
from nautobot.dcim.models import Device, Location, LocationType, Platform
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@10` (lines 10-10)

```python
from nautobot.extras.models import Role, Status
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@12` (lines 12-12)

```python
from nautobot_device_onboarding.exceptions import OnboardException
```

## `nautobot_device_onboarding/tests/test_netdev_keeper.py` — `module:<module>@13` (lines 13-13)

```python
from nautobot_device_onboarding.helpers import onboarding_task_fqdn_to_ip
```

## `nautobot_device_onboarding/tests/test_onboarding.py` — `module:<module>@2` (lines 2-2)

```python
from unittest import mock
```

## `nautobot_device_onboarding/tests/test_onboarding.py` — `module:<module>@5` (lines 5-5)

```python
from django.test import TestCase
```

## `nautobot_device_onboarding/tests/test_onboarding.py` — `module:<module>@6` (lines 6-6)

```python
from django.contrib.contenttypes.models import ContentType
```

## `nautobot_device_onboarding/tests/test_onboarding.py` — `module:<module>@9` (lines 9-9)

```python
from nautobot.dcim.models import Device, Location, LocationType, Platform
```

## `nautobot_device_onboarding/tests/test_onboarding.py` — `module:<module>@10` (lines 10-10)

```python
from nautobot.extras.models import Status
```
