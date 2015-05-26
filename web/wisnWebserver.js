var express = require("express");
var http = require("http");
var mqtt = require("mqtt");
var url = require("url");
var mongoClient = require("mongodb").MongoClient;
var qs = require("querystring");

var app = express();
var server = http.createServer(app);
var io = require("socket.io").listen(server);

var dburl = "mongodb://localhost:27020/wisn";
var db;
var nodesCol
var posCol;
var namesCol;
var eventsTopic = "wisn/events";
var positionsTopic = "wisn/positions";

//process.on('SIGTERM', function () {
//    console.log("Shutting down...");
//    client.end();
    //db.close();
    //mongoClient.close();
//    server.close(function () {
//        process.exit(0);
//    });
//});

//process.on('SIGINT', function () {
//    console.log("Shutting down...");
//    client.end();
    //db.close();
    //mongoClient.close();
//    server.close(function () {
//        process.exit(0);
//    });
//});

mongoClient.connect(dburl, function (err, database) {
    if (err != null) {
        throw err;
    }
    console.log("Connected to DB");
    db = database;
    nodesCol = db.collection("nodes");
    posCol = db.collection("positions");
    namesCol = db.collection("names");
    calCol = db.collection("calibration");
});

app.set("view engine", "jade");
app.locals.pretty = true;
app.use(express.static(__dirname + "/images"));

app.get("/", function (req, res) {
    res.render("map", { title: "Map Test",
                        scripts: ["https://maps.googleapis.com/maps/api/js?v=3",
                                  "/socket.io/socket.io.js",
                                  "wisnNode.js",
                                  "ContextMenu.js",
                                  "wisnMap.js"] });
});

app.get("/edit", function (req, res) {
    res.render("map", { title: "Edit Map Test",
                        scripts: ["https://maps.googleapis.com/maps/api/js?v=3",
                                  "/socket.io/socket.io.js",
                                  "wisnNode.js",
                                  "ContextMenu.js",
                                  "wisnMap.js"] });
});

app.get("/optin", function (req, res) {
    res.render("optin", { title: "Wisn Opt-in" });
});

app.post("/optin", function (req, res) {
    var formData = "";

    req.on("data", function (data) {
        formData += data;
    });

    req.on("end", function () {
        var data = qs.parse(formData);
        if (data["name"] != null && data["mac"] != null) {
            var mac = data["mac"].toUpperCase();
            mac = mac.replace(/(:|-)/g, "");
            namesCol.update({ mac: mac }, { $set: { name: data["name"] } },
                            { upsert: true }, function (err, docs) {

                if (err != null) {
                    throw err;
                }
            });
            client.publish(eventsTopic, "userUpdate");
            res.render("optinsuccess", { title: "Wisn Opt-in Success" });
        }
    });
});

app.get("/optout", function (req, res) {
    res.render("optout", { title: "Wisn Opt-out" });
});

app.post("/optout", function (req, res) {
    var formData = "";

    req.on("data", function (data) {
        formData += data;
    });

    req.on("end", function () {
        var data = qs.parse(formData);
        if (data["mac"] != null) {
            var mac = data["mac"].toUpperCase();
            mac = mac.replace(/(:|-)/g, "");
            namesCol.remove({ mac: mac }, function (err, result) {
                if (err != null) {
                    throw err;
                }
            });
            client.publish(eventsTopic, "userUpdate");
            res.render("optoutsuccess", { title: "Wisn Opt-out Success" });
        }
    });
});

app.get("/users", function (req, res) {
    res.writeHead(200, {
        'Content-Type': 'text/html'
    });
    var dataStream = namesCol.find({}).stream();
    dataStream.on("data", function (data) {
        dataStream.pause();
        if (data != null) {
            res.write(data.mac + "  -  " + data.name + "<br>");
        }
        dataStream.resume();
    });
    dataStream.on("end", function() {
        res.end();
    });
});

app.get("/wisnNode.js", function (req, res) {
    res.sendFile(__dirname + "/js/wisnNode.js");
});

app.get("/ContextMenu.js", function (req, res) {
    res.sendFile(__dirname + "/js/ContextMenu.js");
});

app.get("/wisnMap.js", function (req, res) {
    res.sendFile(__dirname + "/js/wisnMap.js");
});

app.get("/map.css", function (req, res) {
    res.sendFile(__dirname + "/styles/map.css");
});

app.get("/images*", function (req, res) {
    var pathname = url.parse(req.url).pathname;
    res.sendFile(__dirname + pathname);
});

client = mqtt.createClient(1883, "localhost");
client.subscribe(eventsTopic);
client.subscribe(positionsTopic);
client.on("message", function (topic, message) {
    if (topic == positionsTopic) {
        var data = JSON.parse(message);
        sendPosition(data);
    }
});

server.listen(8080);

io.on('connection', function (socket) {
    console.log("Got connection");

    socket.on('setRoom', function (data) {
        socket.join(data);
        console.log("Joined room " + data);
        if (data == 'edit') {
            sendAllNodes(socket);
            sendAllCals(socket);
        } else if (data == "view") {
            sendAllPositions(socket);
        }
    });

    socket.on('addNode', function (data) {
        console.log("Added node " + data.name + " at (" + data.x + ", " + data.y + ")");
        socket.broadcast.to('edit').emit('addNode', data);
        nodesCol.update({ name: data.name }, { $set: { x: data.x, y: data.y } },
                        { upsert: true }, function (err, docs) {

            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "nodeUpdate");
    });

    socket.on('deleteNode', function (data) {
        console.log("Deleted node " + data);
        socket.broadcast.to('edit').emit('deleteNode', data);
        nodesCol.remove({ name: data }, function (err, docs) {
            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "nodeUpdate");
    });

    socket.on('repositionNode', function (data) {
        console.log("Repositioning node " + data.name + " to (" + data.x + ", " + data.y + ")");
        socket.broadcast.to('edit').emit('repositionNode', data);
        nodesCol.update({ name: data.name }, { $set: { x: data.x, y: data.y } },
                        { upsert: true }, function (err, docs) {

            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "nodeUpdate");
    });

    socket.on('addCal', function (data) {
        console.log("Added calibration point " + data.name + " at (" + data.x + ", " + data.y + ")");
        socket.broadcast.to('edit').emit('addCal', data);
        calCol.update({ name: data.name }, { $set: { x: data.x, y: data.y } },
                      { upsert: true }, function (err, docs) {

            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "calibrationUpdate");
    });

    socket.on('deleteCal', function (data) {
        console.log("Deleted calibration point " + data);
        socket.broadcast.to('edit').emit('deleteCal', data);
        calCol.remove({ name: data }, function (err, docs) {
            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "calibrationUpdate");
    });

    socket.on('repositionCal', function (data) {
        console.log("Repositioning calibration point " + data.name + " to (" + data.x + ", " + data.y + ")");
        socket.broadcast.to('edit').emit('repositionCal', data);
        calCol.update({ name: data.name }, { $set: { x: data.x, y: data.y } },
                      { upsert: true }, function (err, docs) {

            if (err != null) {
                throw err;
            }
        });
        client.publish(eventsTopic, "calibrationUpdate");
    });
})

function sendAllNodes(socket) {
    var dataStream = nodesCol.find({}).stream();
    dataStream.on("data", function (data) {
        dataStream.pause();
        socket.emit('addNode', { name: data.name, x: data.x, y: data.y });
        console.log("Sending " + data.name + " at (" + data.x + ", " + data.y + ")");
        dataStream.resume();
    });
}

function sendAllCals(socket) {
    var dataStream = calCol.find({}).stream();
    dataStream.on("data", function (data) {
        dataStream.pause();
        socket.emit('addCal', { name: data.name, x: data.x, y: data.y });
        console.log("Sending " + data.name + " at (" + data.x + ", " + data.y + ")");
        dataStream.resume();
    });
}

function sendAllPositions(socket) {
    var dataStream = posCol.find({}).stream();
    dataStream.on("data", function (data) {
        dataStream.pause();
        namesCol.findOne({mac:data.mac}, function (err, nameData) {
            if (err == null && nameData != null) {
                socket.emit('addDevice', { name: nameData.name, x: data.x, y: data.y, r: data.r });
                console.log("Sending " + nameData.name + " at (" + data.x + ", " + data.y + ")" + " R " + data.r);
            }
        });
        dataStream.resume();
    });
}

function sendPosition(data) {
    namesCol.findOne({mac:data.mac}, function (err, nameData) {
        if (err == null && nameData != null) {
            io.emit('addDevice', { name: nameData.name, x: data.x, y: data.y, r: data.r });
            console.log("Sending " + nameData.name + " at (" + data.x + ", " + data.y + ")" + " R " + data.r);
        }
    });
}
