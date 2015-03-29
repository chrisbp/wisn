var isEdit;

if (startsWith(document.title, "edit")) {
    isEdit = true;
    console.log("Edit mode");
} else {
    isEdit = false;
    console.log("View mode");
}

var nodes = {};         //Map of all nodes
var cals = {};          //Map of all calibration points
var devices = {};     //Map of all device positions

//Socket for comms with server
var socket;

//Sets options for map
var testMapOptions = {
    //Tells gmaps which tiles to load
    getTileUrl: function(coord, zoom) {
        var maxNum = (1 << zoom);
        if (coord.x >= 0 && coord.x < maxNum && coord.y >= 0 && coord.y < maxNum) {
            return "/images/tile_" + zoom + "_" + coord.x + "-" + coord.y + ".png";
        } else {
            return null;
        }
    },
    tileSize: new google.maps.Size(256, 256),
    maxZoom: 3,
    minZoom: 0,
    name: "UQ Building 17 Floor 4"
};

//The type of map to be created
var testMap = new google.maps.ImageMapType(testMapOptions);

//Function that creates the actual map
function initialise() {
    //Sets up initial view options
    var latlong = new google.maps.LatLng(0, 0);
    var mapOptions = {
        center: latlong,
        zoom: 1,
        streetViewControl: false,
        mapTypeControlOptions: {
            mapTypeIds: ['test']
        }
    };

    //Actual map object created here
    var map = new google.maps.Map(document.getElementById('map-canvas'),
        mapOptions);
    map.mapTypes.set('test', testMap);
    map.setMapTypeId('test');

    socket = io();

    if (isEdit) {
        //Setup right click menu for editing
        var mapMenu = setupMapContextMenu(map);
        var nodeMenu = setupNodeContextMenu(map);
        socket.emit('setRoom', "edit");
        createMapMenuListeners(map, mapMenu, nodeMenu);
        createNodeMenuListeners(map, nodeMenu);
        createSocketEventsEdit(map, nodeMenu);
    } else {
        socket.emit('setRoom', "view");
        createSocketEventsView(map, nodeMenu);
    }
}

//Create a new node marker on the map
function addNode(map, latLng, name, nodeMenu, type) {
    if (type == "node") {
        var wisnNode = nodes[name];
    } else if (type == "cal") {
        var wisnNode = cals[name];
    } else if (type == "device") {
        var wisnNode = devices[name];
    }
    var point = map.getProjection().fromLatLngToPoint(latLng);
    if (wisnNode != null) {
        updateNodePosition(map, { name: name, x: point.x, y: point.y }, wisnNode, type);
    } else {
        console.log("Added point " + name + " at X: " + point.x + "  Y: " + point.y);
        var wisnNode = new WisnNode();
        wisnNode.setName(name);
        wisnNode.setPoint(point);
        wisnNode.setType(type);
        //Create new marker at point of right click
        if (type == "node") {
            var marker = new google.maps.Marker({
                icon: "http://maps.google.com/mapfiles/ms/icons/blue-dot.png",
                position: latLng,
                map: map,
                title: name,
                draggable: true
            });
            wisnNode.setMarker(marker);
            //Add new node to nodes map
            nodes[wisnNode.name] = wisnNode;
        } else if (type == "cal") {
            var marker = new google.maps.Marker({
                icon: "http://maps.google.com/mapfiles/ms/icons/green-dot.png",
                position: latLng,
                map: map,
                title: name,
                draggable: true
            });
            wisnNode.setMarker(marker);
            //Add new node to calibration map
            cals[wisnNode.name] = wisnNode;
        } else if (type == "device") {
            var marker = new google.maps.Marker({
                position: latLng,
                map: map,
                title: name,
                draggable: true
            });
            wisnNode.setMarker(marker);
            //Add new node to calibration map
            devices[wisnNode.name] = wisnNode;
        }
        //Create new info window for node marker
        var infoWindow = new google.maps.InfoWindow();
        wisnNode.setInfoWindow(infoWindow);
        updateInfoWindow(wisnNode);
        //Add listener to open info window when node marker is clicked
        google.maps.event.addListener(wisnNode.marker, 'click', function (event) {
            wisnNode.infoWindow.open(map, wisnNode.marker);
        });

        if (type == "node" || type == "cal") {
            //Add listener to node marker to show node menu when right clicked
            google.maps.event.addListener(wisnNode.marker, 'rightclick', function (event) {
                nodeMenu.show(event.latLng);
                nodeMenu.setParentObj(wisnNode);    //Set reference to the node clicked
            });
            //Add listener to node marker to update position when dragged
            google.maps.event.addListener(wisnNode.marker, 'dragend', function (event) {
                var point = map.getProjection().fromLatLngToPoint(wisnNode.marker.getPosition());
                wisnNode.setPoint(point);
                updateInfoWindow(wisnNode);
                console.log("Node " + wisnNode.name  + " now at (" + wisnNode.x + ", " + wisnNode.y + ")");
                if (type == "node") {
                    socket.emit('repositionNode', { name: wisnNode.name, x: wisnNode.x, y: wisnNode.y});
                } else if (type == "cal") {
                    socket.emit('repositionCal', { name: wisnNode.name, x: wisnNode.x, y: wisnNode.y});
                }
            });
        }
    }
    return point;
}

//Delete the given node
function deleteNode(map, wisnNode) {
    if (wisnNode.type == "node") {
        var wisnNode = nodes[wisnNode.name];
    } else if (wisnNode.type == "cal") {
        var wisnNode = cals[wisnNode.name];
    }
    if (wisnNode != null) {
        wisnNode.marker.setMap(null); //Delete the node marker by setting its map reference to null
    }
    if (wisnNode.type == "node") {
        delete nodes[wisnNode.name];
    } else if (wisnNode.type == "cal") {
        delete cals[wisnNode.name];
    } else if (wisnNode.type == "device") {
        delete devices[wisnNode.name];
    }
}

//Delete the node with the matching name given
function deleteNodeByName(map, name, type) {
    if (type == "node") {
        var wisnNode = nodes[name];
    } else if (type == "cal") {
        var wisnNode = cals[name];
    } else if (type == "device") {
        var wisnNode = devices[name];
    }
    if (wisnNode != null) {
        deleteNode(map, wisnNode);
    } else {
        console.log("Couldn't find a match for node to remove");
    }
}

//Update the position of the node with the given name
function updateNodePositionByName(map, data, type) {
    if (type == "node") {
        var wisnNode = nodes[data.name];
    } else if (type == "cal") {
        var wisnNode = cals[data.name];
    } else if (type == "device") {
        var wisnNode = devices[name];
    }
    updateNodePosition(map, data, wisnNode, type);
}

function updateNodePosition(map, data, wisnNode, type) {
    if (wisnNode != null) {
        var latLng = map.getProjection().fromPointToLatLng(new google.maps.Point(data.x, data.y));
        wisnNode.marker.setPosition(latLng);
        wisnNode.setX(data.x);
        wisnNode.setY(data.y);
        updateInfoWindow(wisnNode);
    } else {
        console.log("Couldn't find a match for node to update");
    }
}

function updateInfoWindow(wisnNode) {
    var content = '<div id="infoWindow"><h3>' + wisnNode.name + '</h3>' +
                  '<h4>(' + wisnNode.x.toFixed(1) + ', ' + wisnNode.y.toFixed(1) + ')</h4></div>'
    wisnNode.infoWindow.setContent(content);
}

function startsWith(checkString, searchString) {
    for (var i = 0; i < searchString.length; i++) {
        if (checkString.charAt(i).toLowerCase() != searchString.charAt(i).toLowerCase()) {
            return false;
        }
    }
    return true;
}

function setupMapContextMenu(map) {
    //Setup right click context menu options for the map
    var mapMenuOptions = {};
    mapMenuOptions.classNames = {
        menu: 'contextMenu',
        menuSeparator: 'contextMenuSeparator'
    };
    //Add all menu items for map menu
    var mapMenuItems = [];
    mapMenuItems.push({
        className: 'contextMenuItem',
        eventName: 'addNode',
        label: 'Add node'
    });
    mapMenuItems.push({
        className: 'contextMenuItem',
        eventName: 'addCal',
        label: 'Add calibration point'
    });
    mapMenuOptions.menuItems = mapMenuItems;
    //Create actual context menu
    var mapMenu = new ContextMenu(map, mapMenuOptions);
    //Add listener for when map is right clicked to show context menu
    google.maps.event.addListener(map, 'rightclick', function (event) {
        mapMenu.show(event.latLng);
    });
    return mapMenu;
}

function setupNodeContextMenu(map) {
    //Setup right click context menu options for node markers
    var nodeMenuOptions = {};
    nodeMenuOptions.classNames = {
        menu: 'contextMenu',
        menuSeparator: 'contextMenuSeparator'
    };
    //Add all menu items for node menu
    var nodeMenuItems = [];
    nodeMenuItems.push({
        className: 'contextMenuItem',
        eventName: 'deleteNode',
        label: 'Delete node'
    });
    nodeMenuOptions.menuItems = nodeMenuItems;
    //Create actual context menu
    var nodeMenu = new ContextMenu(map, nodeMenuOptions);
    return nodeMenu;
}

function createMapMenuListeners(map, mapMenu, nodeMenu) {
    //Add listener for when a menu item is clicked for the map menu
    google.maps.event.addListener(mapMenu, 'menu_item_selected', function (latLng, event) {
        if (event == 'addNode') {
            var nodeName = "";
            var msg = "";
            //Ask user for node name
            while (nodeName == "") {
                nodeName = prompt(msg + "Enter node number:");
                if (nodeName == "") {
                    msg = "Error: Node number cannot be blank\n"
                } else {
                    nodeName = "wisn" + nodeName;
                    var wisnNode = nodes[nodeName];
                    if (wisnNode != null) {
                        nodeName = "";
                        msg = "Error: A node with that number already exists\n";
                        break;
                    }
                }
            }
            if (nodeName != null && nodeName != "wisnnull") {
                //Node name is valid so create node marker
                var point = addNode(map, latLng, nodeName, nodeMenu, "node");
                socket.emit('addNode', { name: nodeName, x: point.x, y: point.y });
            }
        } else if (event == 'addCal') {
            var calName = "";
            var msg = "";
            //Ask user for cal name
            while (calName == "") {
                calName = prompt(msg + "Enter calibration name:");
                if (calName == "") {
                    msg = "Error: Calibration name cannot be blank\n"
                } else {
                    var wisnNode = cals[nodeName];
                    if (wisnNode != null) {
                        calName = "";
                        msg = "Error: A calibration point with that name already exists\n";
                        break;
                    }
                }
            }
            if (calName != null) {
                //Calibration name is valid so create node marker
                var point = addNode(map, latLng, calName, nodeMenu, "cal");
                socket.emit('addCal', { name: calName, x: point.x, y: point.y });
            }
        }
    });
}

function createNodeMenuListeners(map, nodeMenu) {
    //Add listener for when a menu item is clicked for the node menu
    google.maps.event.addListener(nodeMenu, 'menu_item_selected', function (latLng, event) {
        if (event == 'deleteNode') {
            var wisnNode = nodeMenu.getParentObj();
            if (wisnNode.type == "node") {
                socket.emit('deleteNode', wisnNode.name);
            } else if (wisnNide.type == "cal") {
                socket.emit('deleteCal', wisnNode.name);
            }
            deleteNode(map, wisnNode);
        }
    });
}

function createSocketEventsEdit(map, nodeMenu) {
    //Add listeners for server
    socket.on('addNode', function (data) {
        console.log("Server: add node at (" + data.x + ", " + data.y + ")");
        var point = new google.maps.Point(data.x, data.y);
        var latLng = map.getProjection().fromPointToLatLng(point);
        addNode(map, latLng, data.name, nodeMenu, "node");
    });

    socket.on('deleteNode', function (data) {
        console.log("Server: delete node " + data);
        deleteNodeByName(map, data, "node");
    });

    socket.on('repositionNode', function (data) {
        console.log("Server: repositioning node " + data.name);
        updateNodePositionByName(map, data, "node");
    });

    socket.on('addCal', function (data) {
        console.log("Server: add calibration point at (" + data.x + ", " + data.y + ")");
        var point = new google.maps.Point(data.x, data.y);
        var latLng = map.getProjection().fromPointToLatLng(point);
        addNode(map, latLng, data.name, nodeMenu, "cal");
    });

    socket.on('deleteCal', function (data) {
        console.log("Server: delete calibration point " + data);
        deleteNodeByName(map, data, "cal");
    });

    socket.on('repositionCal', function (data) {
        console.log("Server: repositioning calibration point " + data.name);
        updateNodePositionByName(map, data, "cal");
    });
}

function createSocketEventsView(map, nodeMenu) {
    //Add listeners for server
    socket.on('addDevice', function (data) {
        console.log("Server: add device at (" + data.x + ", " + data.y + ")");
        var point = new google.maps.Point(data.x, data.y);
        var latLng = map.getProjection().fromPointToLatLng(point);
        addNode(map, latLng, data.name, nodeMenu, "device");
    });

    socket.on('deleteDevice', function (data) {
        console.log("Server: delete device " + data);
        deleteNodeByName(map, data, "device");
    });

    socket.on('repositionDevice', function (data) {
        console.log("Server: repositioning device " + data.name);
        updateNodePositionByName(map, data, "device");
    });
}

google.maps.event.addDomListener(window, 'load', initialise);
