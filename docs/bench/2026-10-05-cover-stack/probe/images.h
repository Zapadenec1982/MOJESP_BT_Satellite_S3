#pragma once
struct image {
    const char*          name;
    const unsigned char* data;
    int                  len;
};
extern const struct image IMAGES[];
extern const int          IMAGE_COUNT;
