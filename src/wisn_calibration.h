#ifndef WISN_CALIBRATION
#define WISN_CALIBRATION

enum calType {CAL_START, CAL_END};

struct wisnCalibration {
    char *name;
    double x;
    double y;
    double calibration;
    enum calType type;
};

#endif
