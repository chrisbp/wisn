var WisnNode = function () {

}

WisnNode.prototype.setMarker = function (marker) {
    this.marker = marker;
}

WisnNode.prototype.setCircle = function (circle) {
    this.circle = circle;
}

WisnNode.prototype.setName = function (name) {
    this.name = name;
}

WisnNode.prototype.setX = function (xPos) {
    this.x = xPos;
}

WisnNode.prototype.setY = function (yPos) {
    this.y = yPos;
}

WisnNode.prototype.setR = function (radius) {
    this.r = radius;
}

WisnNode.prototype.setPoint = function (point) {
    this.x = point.x;
    this.y = point.y;
}

WisnNode.prototype.setInfoWindow = function (infoWindow) {
    this.infoWindow = infoWindow;
}

WisnNode.prototype.setType = function (type) {
    this.type = type;
}
