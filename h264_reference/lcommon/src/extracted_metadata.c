#include "extracted_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <cv.h>
#include <highgui.h>


/* types/struct definition */
typedef void (*ExtractedMetadataFreeFunc) (ExtractedMetadata *);
typedef void (*ExtractedMetadataSerializeFunc) (ExtractedMetadata *, char *);
typedef void (*ExtractedMetadataSaveFunc) (ExtractedMetadata *, const char *);
typedef int  (*ExtractedMetadataGetSerializedSizeFunc) (ExtractedMetadata *);

typedef enum {
  /* 1 byte only */
  ExtractedMetadataYImage = 0x01
} ExtractedMetadataType;

struct _ExtractedMetadata {
  ExtractedMetadataType type;
  ExtractedMetadataFreeFunc free;
  ExtractedMetadataSerializeFunc serialize;
  ExtractedMetadataGetSerializedSizeFunc get_serialized_size;
  ExtractedMetadataSaveFunc save;
};

struct _ExtractedYImage {
  ExtractedMetadata parent;
  /* Image plane - 8 bits depth */
  unsigned char ** y;
  /* Image size*/
  uint16_t width;
  uint16_t height;
};

static const int EXTRACTED_METADATA_TYPE_SIZE = 1;

static ExtractedMetadata * extracted_y_image_deserialize(const char * data, int size);

/*
 ********************************
 * ExtractedMetadata Public API *
 ********************************
 */
void extracted_metadata_free(ExtractedMetadata * metadata)
{
  metadata->free(metadata);
}

int extracted_metadata_get_serialized_size(ExtractedMetadata * metadata)
{
  return metadata->get_serialized_size(metadata) + EXTRACTED_METADATA_TYPE_SIZE;
}

void extracted_metadata_serialize(ExtractedMetadata * metadata, char * serialized_data)
{
  /* lets write the media type (1 byte) */
  *serialized_data = (char) metadata->type;
  metadata->serialize(metadata, serialized_data + EXTRACTED_METADATA_TYPE_SIZE);
}

ExtractedMetadata * extracted_metadata_deserialize(const char * data, int size)
{
  /* first byte is the metadata type */
  ExtractedMetadataType type = (ExtractedMetadataType) *data;
  ExtractedMetadata * ret    = NULL;

  switch (type) 
  {
    case ExtractedMetadataYImage:
      ret = extracted_y_image_deserialize(data + EXTRACTED_METADATA_TYPE_SIZE, size - EXTRACTED_METADATA_TYPE_SIZE);
      break;

    default:
      printf("extracted_metadata_deserialize: cant find the extracted metadata type !!!\n");
  }
 
  return ret;
}

void extracted_metadata_save(ExtractedMetadata * metadata, const char * filename)
{
  metadata->save(metadata, filename);
}

/*
 *********************************
 * ExtractedMetadata Private API *
 *********************************
 */
static void extracted_metadata_init(ExtractedMetadata * metadata, 
                                    ExtractedMetadataFreeFunc free, 
                                    ExtractedMetadataSerializeFunc serialize,
                                    ExtractedMetadataGetSerializedSizeFunc get_serialized_size,
                                    ExtractedMetadataSaveFunc save,
                                    ExtractedMetadataType type)
{
  metadata->free                = free;
  metadata->serialize           = serialize;
  metadata->get_serialized_size = get_serialized_size;
  metadata->save                = save;
  metadata->type                = type;
}


/*
 ********************************
 * ExtractedYImage Public API *
 ********************************
 */

static void extracted_y_image_free(ExtractedMetadata * metadata);
static void extracted_y_image_serialize (ExtractedMetadata * metadata, char * data);
static int extracted_y_image_get_serialized_size(ExtractedMetadata * metadata);
static void extracted_y_image_save(ExtractedMetadata * metadata, const char * filename);

ExtractedYImage * extracted_y_image_new(int width, int height)
{
  ExtractedYImage * metadata = malloc(sizeof(ExtractedYImage));
  unsigned char ** y_rows   = malloc(sizeof(unsigned char *) * height);
  unsigned char * y_plane   = malloc(sizeof(unsigned char) * height * width);
  int row_offset            = 0;
  int row;

  /* Filling the rows */
  for (row = 0; row < height; row++) {
    /* y[0] = y_plane + 0. So y[0] points to the entire allocated plane */
    y_rows[row] = y_plane + row_offset;
    row_offset += width;
  }

  metadata->height = height;
  metadata->width  = width;
  metadata->y      = y_rows;
  
  extracted_metadata_init((ExtractedMetadata *) metadata,
                          extracted_y_image_free,
                          extracted_y_image_serialize,
                          extracted_y_image_get_serialized_size,
                          extracted_y_image_save,
                          ExtractedMetadataYImage);

  return metadata;
}

unsigned char ** extracted_y_image_get_y(ExtractedYImage * img)
{
  return img->y;
}


/*
 ************************************
 * ExtractedYImage private functions *
 ************************************
 */

static void extracted_y_image_free(ExtractedMetadata * metadata)
{
  /* metadata->y[0] points to the begin of the y plane. Easy to free all data */
  ExtractedYImage * img = (ExtractedYImage *) metadata;

  /* freeing the image plane */
  free(img->y[0]);

  /* freeing the rows array */
  free(img->y);
}

static ExtractedMetadata * extracted_y_image_deserialize(const char * data, int size)
{
  uint16_t width;
  uint16_t height;
  int plane_size;
  ExtractedYImage * img = NULL; 

  if (size < (sizeof(uint16_t) * 2)) {
    printf("extracted_y_image_deserialize: invalid serialized ExtractedYImage !!!\n");
    return NULL;
  }

  /* avoid problems with type size and endianness */
  width = ntohs(*((uint16_t *) data));
  data += sizeof(uint16_t);

  /* avoid problems with type size and endianness */
  height = ntohs(*((uint16_t *) data));
  data += sizeof(uint16_t);

  size -= sizeof(uint16_t) * 2;

  plane_size = width * height * sizeof(unsigned char);

  if (size != plane_size) {
    printf("extracted_y_image_deserialize: expected plane_size[%d] but was [%d]!!!\n", size, plane_size);
    return NULL;
  }

  /* Lets create a new empty image */
  img   = extracted_y_image_new(width, height);

  /* lets fill the plane */
  memcpy(img->y[0], data, width * height * sizeof(unsigned char));

  return (ExtractedMetadata *) img;
}

static void extracted_y_image_serialize (ExtractedMetadata * metadata, char * data)
{
  ExtractedYImage * img = (ExtractedYImage *) metadata;

  /* avoid problems with type size and endianness */
  *((uint16_t *) data) = htons(img->width);
  data += sizeof(uint16_t);

  *((uint16_t *) data) = htons(img->height);
  data += sizeof(uint16_t);

  /* Lets copy all the plane. Pretty easy since y[0] points to the begin of the plane */
  memcpy(data, img->y[0], img->width * img->height * sizeof(unsigned char));
}

static int extracted_y_image_get_serialized_size(ExtractedMetadata * metadata)
{
  ExtractedYImage * img = (ExtractedYImage *) metadata;
  return sizeof(uint16_t) + sizeof(uint16_t) + img->width * img->height;
}

static void extracted_y_image_save(ExtractedMetadata * metadata, const char * filename)
{
  /* We are going to use OpenCV to save the image to a file */
  ExtractedYImage * img = (ExtractedYImage *) metadata;
  IplImage * frame      = cvCreateImage(cvSize(img->width, img->height), IPL_DEPTH_8U, 3);

  int row, col;
  int frame_i = 0;
  int save_params[3];

  save_params[0] = CV_IMWRITE_JPEG_QUALITY;
  save_params[1] = 95; /* desired quality , 0 - 100 range */
  save_params[2] = 0;


  /* We must write R G B with the same Y sample. Forming a grayscale image */
  for (row = 0; row < img->height; row++) {

    for (col = 0; col < img->width; col++) {

      frame->imageData[frame_i]     = img->y[row][col];
      frame->imageData[frame_i + 1] = img->y[row][col];
      frame->imageData[frame_i + 2] = img->y[row][col];
      frame_i += 3;
    }
  }

  cvSaveImage(filename, frame, save_params);
  cvReleaseImage(&frame);
}