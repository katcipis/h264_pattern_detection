/*
* This test simply process a raw YUV 4:2:0 video on OpenCV Haar.
* It is usefull to measure the quality of openCV using different video configurations (resolution, quality, etc).
* It will dump the found objects on jpeg files, and report how much interest objects have been found.
* 
* @author Tiago Katcipis <tiagokatcipis@gmail.com>. 
*
*/
#include <sys/stat.h>
#include <sys/types.h>
#include <cv.h>	      /* for OpenCV basic structures for ex.: IPLImage */
#include <highgui.h>  /* for OpenCV functions like cvCvtColor */
#include <glib.h>
#include <stdio.h>
#include <errno.h>


#define FOUND_OBJECTS_BASE_DIR "found_objects"

static const int NUM_CHANNELS                = 3;

static const int MIN_OBJECT_WIDTH            = 30;
static const int MIN_OBJECT_HEIGHT           = 30;

static const int MAX_OBJECT_WIDTH            = 800;
static const int MAX_OBJECT_HEIGHT           = 800;



static void save_detected_objects(IplImage * image, CvSeq* results, const gchar* objects_dir)
{
  static int saved_objects = 0;
  int i;
  int parameters[3];

  for (i = 0; i < results->total; i++) {

    gchar * object_filename  = g_strdup_printf("%s/object_%d.jpg", objects_dir, saved_objects);
     
    saved_objects++;
    parameters[0] = CV_IMWRITE_JPEG_QUALITY;
    parameters[1] = 100;
    parameters[2] = 0;

    cvSetImageROI(image, *((CvRect*)cvGetSeqElem(results, i)));
    cvSaveImage(object_filename, image, parameters);  

    cvResetImageROI(image);
    g_free(object_filename);

  }

}

static void create_detected_objects_directory(const gchar * directory_path)
{
  int ret = mkdir(directory_path, S_IRWXU | S_IRWXO);

  if (ret == -1 && errno != EEXIST) {

    printf("Erro[%d] criando diretorio[%s].\n", errno, directory_path);
    perror("Error.\n");
    exit(-1);
  }
}

gchar * get_found_objects_subfolder(const gchar * input_filename)
{
  gchar ** splitted = g_strsplit(input_filename, "/", -1);
  gchar ** iter     = splitted;
  gchar * ret       = NULL;

  if (!iter) {
    printf("Something went severelly wrong parsing filename[%s]!!!\n", input_filename);
    exit(-1);
  } 

  /* We want the last part */
  while (*(iter + 1)) {
    iter++;
  }

  ret = g_strdup(*iter);

  g_strfreev(splitted);

  return ret;
}


int main(int argc, char **argv)
{
  /* OpenCV Haar config. */
  CvHaarClassifierCascade * classifier = NULL;
  CvMemStorage * storage = NULL;
  double scale_factor    = 1.1f;
  int min_neighbors      = 3;
  int haar_flags         = CV_HAAR_DO_CANNY_PRUNING; 

  CvSize min_size = {.width = MIN_OBJECT_WIDTH,
                     .height = MIN_OBJECT_HEIGHT};

  CvSize max_size = {.width = MAX_OBJECT_WIDTH,
                     .height = MAX_OBJECT_HEIGHT};
 
  /* Test stuff */
  gchar * buffer                  = NULL;
  gchar * found_objects_dir       = NULL;
  gchar * found_objects_subfolder = NULL;
  IplImage * image                = NULL;
  IplImage * gray_image           = NULL;
  GTimer * timer                  = NULL;
  FILE * input_video_file         = NULL;
  int total_objects_found         = 0;  
  int height                      = 0;
  int width                       = 0;
  int image_size                  = 0;
  int total_images                = 0;
  gdouble min_elapsed             = G_MAXDOUBLE;
  gdouble max_elapsed             = 0;
  gdouble total_elapsed           = 0;
  

  if (argc < 5) {
    printf("Usage: %s <haar training filename> <video file name> <width> <height>\n", argv[0]);
    printf("The video file must be RAW YUV 4:2:0 (YUV - I420). Bit depth must be 8. \n");
    printf("Create a found_objects folder where you are going to execute the tests \n\n");
    return -1;
  }

  width     = atoi(argv[3]);
  height    = atoi(argv[4]);

  input_video_file        = fopen(argv[2], "r");
  found_objects_subfolder = get_found_objects_subfolder(argv[2]);
  found_objects_dir       = g_strdup_printf(FOUND_OBJECTS_BASE_DIR"/%s", found_objects_subfolder);

  create_detected_objects_directory(FOUND_OBJECTS_BASE_DIR);
  create_detected_objects_directory(found_objects_dir);

  buffer           = g_slice_alloc(width * height);
  image            = cvCreateImage(cvSize(width, height), IPL_DEPTH_8U, NUM_CHANNELS);
  gray_image       = cvCreateImage(cvSize(width, height), IPL_DEPTH_8U, 1);

  printf("\nStarting Haar test, width[%d] height[%d] object min size[%d][%d] max size[%d][%d]"
         " scale factor[%f] min_neighbors[%d]\n\n", width, height, min_size.height, min_size.width,
         max_size.height, max_size.width, scale_factor, min_neighbors);

  /* Luma is full resolution, 2 croma are quarter resolution each */
  image_size = width * height;
  storage    = cvCreateMemStorage(0);
  classifier = (CvHaarClassifierCascade*) cvLoad(argv[1], 0, 0, 0 );
  timer      = g_timer_new();

  /* Lets read only the luma plane */
  while (fread(buffer, 1, image_size, input_video_file) == image_size) {

    CvSeq* results     = NULL;
    gdouble elapsed    = 0; 
    gchar * imageData  = image->imageData;
    gchar * luma_plane = buffer;
    int row, col;
   
    /* We must write R G B with the same Y sample. Just as is done on the MetadataExtractor */
    for (row = 0; row < height; row++) {

      for (col = 0; col < width; col++) {
        imageData[0] = luma_plane[col];
        imageData[1] = luma_plane[col];
        imageData[2] = luma_plane[col];
        imageData += 3;

      }

      luma_plane += width;
    }

    cvCvtColor(image, gray_image, CV_BGR2GRAY);

    /* cvEqualizeHist spreads out the brightness values necessary because the integral image 
     features are based on differences of rectangle regions and, if the histogram is not balanced, 
     these differences might be skewed by overall lighting or exposure of the test images. */
    cvEqualizeHist(gray_image, gray_image);

    g_timer_start(timer);

    results =  cvHaarDetectObjects (gray_image,
                                    classifier,
                                    storage,
                                    scale_factor,
                                    min_neighbors,
                                    haar_flags,
                                    min_size,
                                    max_size);

    elapsed = g_timer_elapsed (timer, NULL);

    if (elapsed < min_elapsed) {
      min_elapsed = elapsed; 
    }
 
    if (elapsed > max_elapsed) { 
      max_elapsed = elapsed;
    }

    save_detected_objects(image, results, found_objects_dir);

    total_elapsed += elapsed;
    total_objects_found += results->total;
    total_images++;

    /* Results belongs to the storage, it will be freed later.
       Lets jump the croma information. */
    fseek(input_video_file, image_size / 2, SEEK_CUR);
  }

  if (!feof(input_video_file)) {
    g_error(">>>>>>> An error occured while reading the images from the video file !!!\n");
  }

  cvReleaseImage(&image);
  cvReleaseImage(&gray_image);
  cvClearMemStorage(storage);
  g_slice_free1(width * height, buffer); 
  g_free(found_objects_dir);
  g_free(found_objects_subfolder);
  fclose(input_video_file);

  printf("\n\n==========================================================================================================\n");
  printf("Identified [%d] objects on a video with [%d] frames \n", total_objects_found, total_images);
  printf("Haar profiling (seconds): min elapsed[%f] max elapsed[%f] total elapsed[%f] mean elapsed[%f]\n", 
         min_elapsed, max_elapsed, total_elapsed, total_elapsed / total_images);
  printf("==============================================================================================================\n\n");

  return 0;
}

