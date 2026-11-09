# wood_config 0.1.0

Application configuration in pure wood. Listed in the catalog as `wood@config`.

```sh
wood add wood@config
```

```wood
import config.config as config

settings = config.load("app.conf", "MYAPP_")
port = config.number(settings, "server.port", 8080)
debug = config.flag(settings, "server.debug")
```

```ini
# app.conf
[server]
port = 9000
debug = yes
```

With the prefix `MYAPP_`, the variable `MYAPP_SERVER_PORT` overrides `server.port`.
`number` and `flag` raise an error naming the key when a value has the wrong type,
rather than guessing. A missing file is treated as empty, so defaults still apply.

Status: experimental. The format is a small INI subset, with no quoting, multi-line
values or includes.

Licensed MIT OR Apache-2.0.
