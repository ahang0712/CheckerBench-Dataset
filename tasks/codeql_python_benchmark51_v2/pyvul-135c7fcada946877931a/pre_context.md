# Patch-overlapping Python context before the fix

## `ceilometer/agent.py` — `class:ConfigManagerBase` (lines 45-65)

```python
class ConfigManagerBase(object):
    """Base class for managing configuration file refresh"""

    def __init__(self, conf):
        self.conf = conf

    def load_config(self, cfg_file):
        """Load a configuration file and set its refresh values."""
        if os.path.exists(cfg_file):
            cfg_loc = cfg_file
        else:
            cfg_loc = self.conf.find_file(cfg_file)
            if not cfg_loc:
                LOG.debug("No pipeline definitions configuration file found! "
                          "Using default config.")
                cfg_loc = pkg_resources.resource_filename(
                    __name__, 'pipeline/data/' + cfg_file)
        with open(cfg_loc) as fap:
            conf = yaml.safe_load(fap)
        LOG.info("Config file: %s", conf)
        return conf
```

## `ceilometer/agent.py` — `function:ConfigManagerBase.load_config` (lines 51-65)

```python
    def load_config(self, cfg_file):
        """Load a configuration file and set its refresh values."""
        if os.path.exists(cfg_file):
            cfg_loc = cfg_file
        else:
            cfg_loc = self.conf.find_file(cfg_file)
            if not cfg_loc:
                LOG.debug("No pipeline definitions configuration file found! "
                          "Using default config.")
                cfg_loc = pkg_resources.resource_filename(
                    __name__, 'pipeline/data/' + cfg_file)
        with open(cfg_loc) as fap:
            conf = yaml.safe_load(fap)
        LOG.info("Config file: %s", conf)
        return conf
```
