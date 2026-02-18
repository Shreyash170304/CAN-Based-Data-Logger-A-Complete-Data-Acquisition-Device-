# Web Server API

Base URL: `http://192.168.10.1`

## GET /
- Returns the HTML control panel.

## GET /files
- Returns JSON array of files.
- Looks in `/CAN_Logged_Data/` and root for compatibility.

Example response:
```json
[
  {"name":"CAN_Logged_Data/CAN_LOG_20260129_120001.NXT","size":123456},
  {"name":"old_file.NXT","size":98765}
]
```

## GET /download?file=<path>
- Downloads a file from SD.
- `file` can be `CAN_Logged_Data/<name>` or a root filename.

## POST /delete
- Deletes files listed in JSON body.

Request body:
```json
{"files":["CAN_Logged_Data/file1.NXT","CAN_Logged_Data/file2.NXT"]}
```

Response:
```json
{"success":true,"deleted":2,"failed":0}
```

## GET /folder?path=<path>
- Lists a folder on the SD card.
- Default path is `/CAN_Logged_Data`.

Example response:
```json
[
  {"name":"CAN_LOG_20260129_120001.NXT","path":"/CAN_Logged_Data/CAN_LOG_20260129_120001.NXT","isDir":false,"size":123456}
]
```

## GET /live?limit=<n>&since=<seq>
- Returns live CAN frames.
- `limit` default is 50, max is 200.
- `since` returns only frames after a sequence number.

Example response:
```json
{
  "status":"ok",
  "latest":1024,
  "frames":[
    {"seq":1024,"time":"2026-01-29 12:00:01","unix":1769678401,"micros":123456,
     "id":"100","extended":false,"rtr":false,"dlc":8,"data":[1,2,3,4,5,6,7,8]}
  ]
}
```
