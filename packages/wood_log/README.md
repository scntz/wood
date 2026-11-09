# wood_log 0.1.0

Leveled application logging in pure wood. Listed in the catalog as `wood@log`.

```sh
wood add wood@log
```

```wood
import log.log as logging

logger = logging.logger("server", "info", "server.log")
logging.info(logger, "listening", {"port":"8080"})
logging.failure(logger, "request failed", {"status":"500"})
```

Records go to stderr and, when a file is given, are appended to it, one record per
line. Newlines in messages and values are escaped, so input cannot forge a log line.
Levels are `debug`, `info`, `warn` and `error`.

Status: experimental. There is no rotation, timestamping or asynchronous sink.

Licensed MIT OR Apache-2.0.
