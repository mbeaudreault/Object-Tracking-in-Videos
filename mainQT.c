// Nick Ryan and Matt Beaudreault
//Version 4

//  mainQT.c
//	This program plays a movie file into an ImageStruct and does some simple
//	image processing, just to demonstrate how it's done.
//
//	Keyboard controls:
//		- 'ESC' as usual terminates execution;
//		- space bar: pauses/resumes movie playing
//		- 'n' toggles on/off computation of negative image
//  Final Project CSC412 - Spring 2018
//
//  Created by Jean-Yves Hervé on 2018-05-01.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include <lqt/lqt.h>	//	libquicktime header
//
#include "gl_frontEnd.h"
#include "fileIO_TGA.h"
#include "Blob.h"


//The threads that will take specific parts of the image and calculate the difference of
//2 pictures
typedef struct DifferenceThreadInfo
{
	pthread_t threadID;
	int threadIndex;
	int threadStartingPoint;
	int threadEndingPoint;
} DifferenceThreadInfo;

typedef struct BlobThreadInfo
{
	pthread_t threadID;
	int threadIndex;
	int threadXStartingPoint;
	int threadXEndingPoint;
	int threadYStartingPoint;
	int threadYEndingPoint;

	int blobColorRed;
	int blobColorBlue;
	int blobColorGreen;
} BlobThreadInfo;

typedef enum DisplayChoice {
		SHOW_FRAME,
		SHOW_BACKGROUND,
		SHOW_DIFFERENCE
} DisplayChoice;


//==================================================================================
//	Function prototypes
//==================================================================================

void initializeApplication(void);
void* getBackgroungImage(void* arg);
int movieIsFinished(struct quicktime_s* libFile);

/**	returns a properly initialized ImageStruct.
 *	@param	width	number of columns of the image
 *	@param	height	number of rows of the image
 *	@param	type	one of RGBA32_RASTER, GRAY_RASTER, or FLOAT_RASTER
 *	@param	wordSizeRowPadding word size at which rows should be rounded up.
 *				You shouldn't have any reason to take a value other than 1
 */
ImageStruct newImage(unsigned int width, unsigned int height, ImageType type,
					 unsigned int wordSizeRowPadding);

void getCurrentDifferenceImage(void);

void findEdgeBlobs(void);
void trackBlobs(int index);
void detectBlob(int x, int y, int index);
void trackDetectBlob(int x, int y, int index);

int getLeft(int x, int y);
int getRight(int x, int y);
void fill(int xL, int xR, int y, int index);
int findFirstLeft(int xL, int xR, int y);

int findXMax(int xMax, int xR);
int findXMin(int xMin, int xL);

//==================================================================================
//	Application-level global variables
//==================================================================================

//	Don't touch. These are defined in the front end source code
extern int	gMainWindow;
extern const int WINDOW_WIDTH, WINDOW_HEIGHT;

//	In this handout code, I "play" a move file into an ImageStruct
#define TRACK 0
struct quicktime_s* libFile;
ImageStruct movieFrame, grayMovieFrame, grayBackgroundImage, floatBackgroundImage, currentDifferenceImage;
unsigned int frameIndex;
unsigned int frameWidth, frameHeight;
float scaleX, scaleY;
int initDone = 0;
int isPlaying=0;
int computeNegative=0;
int backgroundFrameCounter = 0;

DisplayChoice displayChoice = SHOW_FRAME;

//pixel arrays that hold the color of that specific pixel
int** movieFramePixel2D;
unsigned char** grayMovieFramePixel2D;
unsigned char** grayBackgroundPixel2D;
float* floatBackgroundPixel2D;
unsigned char** differenceImagePixel2D;


//	A hand-initialized list of blobs, for rendering testing
Blob* blobList;
unsigned int maxNumBlobs = 15;
unsigned int numBlobs = 0;

//syncing stuff
unsigned int maxNumThreads;
int isTimeToSwapGrid = 0;
DifferenceThreadInfo* threads;
BlobThreadInfo* blobThreads;

//mutexes
pthread_mutex_t imageLock;
pthread_mutex_t isTimeToSwapGridLock;
pthread_mutex_t* isThreadDone;
pthread_mutex_t numBlobsMutex;

//------------------------------------------------------------------
//	The constants defined here are for you to modify and add to
//------------------------------------------------------------------
#define IN_PATH		"./DataSets/Series02/"
#define OUT_PATH	"./Output/"

#define MOVIE_PATH "./roadSegment-medium.mov"

//	This is the rendering function in single-pane mode
void displayImagePane(void)
{
	//==============================================
	//	This is OpenGL/glut magic.  Don't touch
	//==============================================
	glutSetWindow(gMainWindow);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (initDone)
	{
		//	Movie frames appear upside-down, so I need to do the scaling
		//	trick
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glRasterPos3f(0.f, WINDOW_HEIGHT, -1.f);
		glPixelZoom(scaleX, -scaleY);

		// MOVED decode into multithreaded function

		//	I had completely forgotten about this because I developed the
		//	libquicktime wrapper class of my Computer Vision library years ago.
		//	This is the only example of an library manipulatingimage/video/movie
		//	data I know of that works with a 2D array rather than a 1D raster.
		//	So, this forced me to enable the 2D raster in my ImageStruct data
		//	type and implement the allocation.

		//==============================================
		//	This is OpenGL/glut magic.  Replace, if you
		//	want, but don't modify
		//==============================================
	/*	if (movieFrame.type == RGBA32_RASTER)
		{
			if (computeNegative)
			{
				int* pixel = (int*) movieFrame.raster;
				for (unsigned int k=0; k< movieFrame.nbRows*movieFrame.nbCols; k++)
				{
					unsigned char* rgba = (unsigned char*) (pixel + k);
					rgba[0] = 0xFF - rgba[0];
					rgba[1] = 0xFF - rgba[1];
					rgba[2] = 0xFF - rgba[2];
				}
			}
			glDrawPixels(movieFrame.nbCols, movieFrame.nbRows,
						  GL_RGBA,
						  GL_UNSIGNED_BYTE,
						  movieFrame.raster);
		}
		else if (movieFrame.type == GRAY_RASTER)
		{
			if (computeNegative)
			{
				unsigned char* pixel = (unsigned char*) movieFrame.raster;
				for (unsigned int k=0; k< movieFrame.nbRows*movieFrame.nbCols; k++)
				{
					pixel[k] = 0xFF - pixel[k];
				}
			}
			*/

			switch (displayChoice) {
				case SHOW_FRAME:
					glDrawPixels(movieFrame.nbCols, movieFrame.nbRows,
							  GL_RGBA,
							  GL_UNSIGNED_BYTE,
							  movieFrame.raster);
					break;

				case SHOW_BACKGROUND:
					glDrawPixels(movieFrame.nbCols, movieFrame.nbRows,
								  GL_LUMINANCE,
								  GL_UNSIGNED_BYTE,
								  grayBackgroundImage.raster);
					break;

				case SHOW_DIFFERENCE:
					glDrawPixels(movieFrame.nbCols, movieFrame.nbRows,
							     GL_LUMINANCE,
							     GL_UNSIGNED_BYTE,
						         currentDifferenceImage.raster);
						break;

			}

			glPushMatrix();
			glTranslatef(0, WINDOW_HEIGHT, 0.f);
			glScalef(scaleX, -scaleY, 1.f);
		  pthread_mutex_lock(&numBlobsMutex);
			for (unsigned int k=0; k<numBlobs && k< maxNumBlobs; k++){
				pthread_mutex_lock(&imageLock);
				renderBlob(blobList + k);
				pthread_mutex_unlock(&imageLock);

			}
			glPopMatrix();

			for (unsigned int k=0; k<numBlobs && k< maxNumBlobs; k++){
				pthread_mutex_lock(&imageLock);
				deleteBlob(blobList + k);
				pthread_mutex_unlock(&imageLock);
			}
			numBlobs = 0;
			pthread_mutex_unlock(&numBlobsMutex);

		glPopMatrix();
	}

	glutSetWindow(gMainWindow);
}


//	This callback function is called when a keyboard event occurs
//	You can change things here if you want to have keyboard input
//
void myKeyboard(unsigned char c, int x, int y)
{
	int ok = 0;

	switch (c)
	{
		//	'esc' to quit
		case 27:
			writeTGA("./frame.tga", &currentDifferenceImage);
			exit(0);
			break;

		//	space bar to pause/resume playing
		case ' ':
			isPlaying = !isPlaying;
			break;

		//	'n' toggles on/off the computation of the negative image
		case 'n':
			computeNegative = !computeNegative;
			break;


			case 'c':
			case 'f':
				displayChoice = SHOW_FRAME;
				break;

			case 'b':
				displayChoice = SHOW_BACKGROUND;
				break;

			case 'd':
				displayChoice = SHOW_DIFFERENCE;
				break;

			default:
				ok = 1;
				break;
	}
	if (!ok)
	{
		//	do something?
	}

	//==============================================
	//	This is OpenGL/glut magic.  Don't touch
	//==============================================
	glutSetWindow(gMainWindow);
	glutPostRedisplay();
}

//------------------------------------------------------------------------
//	You shouldn't have to change anything in the main function.
//------------------------------------------------------------------------
int main(int argc, char** argv)
{
	 if(argc < 2){
		printf("\tIncorrect number of arguments.\n");
		return 1;
	}

		//get the number of threads
	maxNumThreads = atoi(argv[1]);
		//allocate memory for threads
	threads = (DifferenceThreadInfo*) calloc(maxNumThreads, sizeof(DifferenceThreadInfo));
	isThreadDone = (pthread_mutex_t*) calloc(maxNumThreads, sizeof(pthread_mutex_t));

	blobThreads = (BlobThreadInfo*) calloc(maxNumBlobs, sizeof(BlobThreadInfo));

	//	seed the pseudo-random generator
	//	Yes, I am using the C random generator, although I usually rant on and on
	//	that the C/C++ default random generator is junk and that the people who use it
	//	are at best fools.  Here I am not using it to produce "serious" data (as in a
	//	simulation), only some color, in meant-to-be-thrown-away code
	srand((unsigned int) time(NULL));

	//	Even though we extracted the relevant information from the argument
	//	list, I still need to pass argc and argv to the front-end init
	//	function because that function passes them to glutInit, the required call
	//	to the initialization of the glut library.
	initializeFrontEnd(argc, argv);

	//	We perform application-level initialization
	initializeApplication();

	pthread_mutex_init(&imageLock, NULL);
	pthread_mutex_init(&isTimeToSwapGridLock, NULL);
	pthread_mutex_init(&numBlobsMutex, NULL);
	//Create the threads and assign them their specific pixels in the movieFrame
	for(int i = 0; i < maxNumThreads; i++){
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		threads[i].threadIndex = i;
		//give the thread a set of pixels to calculate
		threads[i].threadStartingPoint = i * ceil(((1.0*movieFrame.nbRows / maxNumThreads)));
		if(i == maxNumThreads - 1) {
			threads[i].threadEndingPoint = movieFrame.nbRows;
		}else{
			threads[i].threadEndingPoint = (i+1) * ceil(((1.0*movieFrame.nbRows/maxNumThreads)));
		}

		pthread_mutex_init(&isThreadDone[i], NULL);

		int err = pthread_create(&threads[i].threadID, NULL, getBackgroungImage, &threads[i]);
		if(err != 0){
			printf("\terror creating thread\n");
		}
	}

	//==============================================
	//	This is OpenGL/glut magic.  Don't touch
	//==============================================
	//	Now we enter the main loop of the program and to a large extend
	//	"lose control" over its execution.  The callback functions that
	//	we set up earlier will be called when the corresponding event
	//	occurs
	glutMainLoop();

	//	This will probably never be executed (the exit point will be in one of the
	//	call back functions).
	return 0;
}

// Multithreaded function. Computes the background image.
void* getBackgroungImage(void* arg){
	//to sync use a process similar to assignment three:
		//give each thread a set of rows to work with, loop through every pixle in those rows
		//subtract the pixels by each other and place that new value onto the difference image
	DifferenceThreadInfo *currentThread_ptr = (DifferenceThreadInfo*) arg;
	DifferenceThreadInfo currentThread = *currentThread_ptr;

 // Loop until all frames of the movie have been played.
	while(!movieIsFinished(libFile)){
		pthread_mutex_lock(isThreadDone + currentThread.threadIndex);

    // Nested for loops - change the movie to grayscale and get background image by averaging all pixels
		for (int i = currentThread.threadStartingPoint; i < currentThread.threadEndingPoint; i++){
			for (int j = 0; j< movieFrame.nbCols; j++){
				// Convert to grayscale
				unsigned char* rgba = (unsigned char*) (movieFramePixel2D[i]+j);
				grayMovieFramePixel2D[i][j] = (unsigned char) ((rgba[0] + rgba[1] + rgba[2])/3);

				// Compute background image
				if(backgroundFrameCounter <= 30){
					floatBackgroundPixel2D[(i * floatBackgroundImage.nbCols) + j] += (float)(grayMovieFramePixel2D[i][j]);
				}else if(backgroundFrameCounter == 31){
					floatBackgroundPixel2D[(i * floatBackgroundImage.nbCols) + j] = floatBackgroundPixel2D[(i * floatBackgroundImage.nbCols) + j] / 30;

					grayBackgroundPixel2D[i][j] = (unsigned char) (floatBackgroundPixel2D[(i * floatBackgroundImage.nbCols) + j]);
				}
			}
		}

	//Check to make sure all of the threads have executed before swapping the grid
		pthread_mutex_lock(&isTimeToSwapGridLock);
		isTimeToSwapGrid += 1;
		// Is this the final thread ? - if so then start code to play the video
		if(isTimeToSwapGrid == maxNumThreads){
			backgroundFrameCounter++;
			isTimeToSwapGrid = 0;

			pthread_mutex_unlock(&isTimeToSwapGridLock);
			// Decode each fram of the vide in here as long as it is set to be playing
			if (isPlaying) {
				//pthread_mutex_lock(&imageLock);
				lqt_decode_video(libFile, (unsigned char**) (movieFrame.raster2D), TRACK);
				getCurrentDifferenceImage();

				if(backgroundFrameCounter > 31){
					pthread_mutex_lock(&numBlobsMutex);
					for (unsigned int k=0; k<numBlobs && k< maxNumBlobs; k++){
						deleteBlob(blobList + k);
					}
					numBlobs = 0;
					for(int i = 0; i < numBlobs && i < maxNumBlobs;i++){
						trackBlobs(i);
					}
					pthread_mutex_unlock(&numBlobsMutex);
										findEdgeBlobs();
				}
				// Unlock all threads
				for(int i = 0; i < maxNumThreads; i++){
					pthread_mutex_unlock(isThreadDone + i);
				}

			}
		//if threads is not the last thread to execute lock itself
		}else{
			// If not the final thread, allow others to finish
			pthread_mutex_unlock(&isTimeToSwapGridLock);
			usleep(10000);
		}
	}
	return 0;
}


//Check if the movie has finished playing in the display panel -jyh
int movieIsFinished(struct quicktime_s* libFile){

	long total = quicktime_video_length(libFile, TRACK);

	long current = quicktime_video_position(libFile, TRACK);

  return (current >= total);

}

void getCurrentDifferenceImage(void){
	for (int i = 0; i < movieFrame.nbRows; i++){
		for (int j = 0; j<movieFrame.nbCols; j++){

			differenceImagePixel2D[i][j] = (unsigned char) abs(grayMovieFramePixel2D[i][j] - grayBackgroundPixel2D[i][j]);

			if(differenceImagePixel2D[i][j] > 40){
				differenceImagePixel2D[i][j] = 255;
			}
			else{
				differenceImagePixel2D[i][j] = 0;
			}
		}
	}
}


void findEdgeBlobs(void) {
	for(int i = 0; i < movieFrame.nbRows; i += 14){
		for(int j = (movieFrame.nbCols - (movieFrame.nbCols / 8)); j < movieFrame.nbCols; j+= 14){
			if(differenceImagePixel2D[i][j] == 255){
				pthread_mutex_lock(&numBlobsMutex);
				if(numBlobs <= maxNumBlobs){

					detectBlob(j, i, numBlobs);
				}
				pthread_mutex_unlock(&numBlobsMutex);

			}
		}
		for(int j = 0; j < movieFrame.nbCols/8; j+= 14){
			if(differenceImagePixel2D[i][j] == 255){
				pthread_mutex_lock(&numBlobsMutex);
				if(numBlobs <= maxNumBlobs){
					detectBlob(j, i, numBlobs);
				}
				pthread_mutex_unlock(&numBlobsMutex);

			}
		}

	}
}
void trackBlobs(int index){
	printf("trackblobs called index = %d\n", index);

	// Change from edge blobs to middle blobs

	for(int i = blobThreads[index].threadYStartingPoint; i < blobThreads[index].threadYEndingPoint; i++){
		for(int j = blobThreads[index].threadXStartingPoint; j < blobThreads[index].threadXEndingPoint; j++){
			if(differenceImagePixel2D[i][j] == 255){
				trackDetectBlob(j, i, numBlobs);
			}
		}
	}
}

void trackDetectBlob(int x, int y, int index){
	pthread_mutex_lock(&imageLock);

	blobList[index] = newBlob();

	blobList[index].xMax = 0;
	blobList[index].xMin = movieFrame.nbCols;

	int xR = x;
	int xL = x;
	xR = getRight(xR, y);
	xL = getLeft(xL, y);

	fill(xL, xR, y, index);

	ExtentStack myStack = newExtentStack();
	//pthread_mutex_lock(&imageLock);

	if(y > 0){
		addSegmentToStack(&myStack, xL, xR, y - 1);
	}
	if(y < movieFrame.nbRows -1){
		addSegmentToStack(&myStack, xL, xR, y + 1);
	}
	//pthread_mutex_unlock(&imageLock);


	while(!stackIsEmpty(&myStack)){
		Extent top = popStack(&myStack);

		top.xL = findFirstLeft(top.xL, top.xR, top.y);

		while(top.xL <= top.xR){
				int r = getRight(top.xL, top.y);
				fill(top.xL, r, top.y, index);
				if(top.y > 0){
					addSegmentToStack(&myStack, top.xL, r, top.y - 1);
				}//end if
				if(top.y < movieFrame.nbRows -1){
					addSegmentToStack(&myStack, top.xL, r, top.y + 1);
				}//end if

				if (r+2 <= top.xR){
					top.xL = findFirstLeft(r + 2, top.xR, top.y);
				}
				else{
					top.xL = top.xR+1;
				}
				if(top.xL == movieFrame.nbCols && top.y == movieFrame.nbRows - 1){
					while(!stackIsEmpty(&myStack)){
						popStack(&myStack);
					}
				}
		}//end while
	}//endwhile
	if(blobList[index].nbPixels > 200){
		//const unsigned int blobHeight = blobList[index]->yBottom - blob->yTop + 1;

		blobList[index].red = blobThreads[index].blobColorRed;
		blobList[index].green = blobThreads[index].blobColorGreen;
		blobList[index].blue = blobThreads[index].blobColorBlue;

		blobThreads[index].threadXStartingPoint = blobList[index].xMin;
		blobThreads[index].threadXEndingPoint = blobList[index].xMax;
		blobThreads[index].threadYStartingPoint = blobList[index].yBottom;
		blobThreads[index].threadYEndingPoint = blobList[index].yTop;

	}else{
		//deleteBlob(&blobList[index]);
	}
	pthread_mutex_unlock(&imageLock);
}//end function

void detectBlob(int x, int y, int index){
	pthread_mutex_lock(&imageLock);

	blobList[index] = newBlob();

	blobList[index].xMax = 0;
	blobList[index].xMin = movieFrame.nbCols;

	int xR = x;
	int xL = x;
	xR = getRight(xR, y);
	xL = getLeft(xL, y);

	fill(xL, xR, y, index);

	ExtentStack myStack = newExtentStack();
	//pthread_mutex_lock(&imageLock);

	if(y > 0){
		addSegmentToStack(&myStack, xL, xR, y - 1);
	}
	if(y < movieFrame.nbRows -1){
		addSegmentToStack(&myStack, xL, xR, y + 1);
	}
	//pthread_mutex_unlock(&imageLock);


	while(!stackIsEmpty(&myStack)){
		Extent top = popStack(&myStack);

		top.xL = findFirstLeft(top.xL, top.xR, top.y);

		while(top.xL <= top.xR){
				int r = getRight(top.xL, top.y);
				fill(top.xL, r, top.y, index);
				if(top.y > 0){
					addSegmentToStack(&myStack, top.xL, r, top.y - 1);
				}//end if
				if(top.y < movieFrame.nbRows -1){
					addSegmentToStack(&myStack, top.xL, r, top.y + 1);
				}//end if

				if (r+2 <= top.xR){
					top.xL = findFirstLeft(r + 2, top.xR, top.y);
				}
				else{
					top.xL = top.xR+1;
				}
				if(top.xL == movieFrame.nbCols && top.y == movieFrame.nbRows - 1){
					while(!stackIsEmpty(&myStack)){
						popStack(&myStack);
					}
				}
		}//end while
	}//endwhile
	if(blobList[index].nbPixels > 200){

		numBlobs += 1;

		int color = rand() % 3;

		if(color == 0)
			blobList[index].red = 255;
		if(color == 1)
			blobList[index].green = 255;
		if(color == 2)
			blobList[index].blue = 255;

		//const unsigned int blobHeight = blobList[index]->yBottom - blob->yTop + 1;

		blobThreads[index].threadIndex = index;

		blobThreads[index].blobColorRed = blobList[index].red;
		blobThreads[index].blobColorGreen = blobList[index].green;
		blobThreads[index].blobColorBlue = blobList[index].blue;

		blobThreads[index].threadXStartingPoint = blobList[index].xMin;
		blobThreads[index].threadXEndingPoint = blobList[index].xMax;
		blobThreads[index].threadYStartingPoint = blobList[index].yBottom;
		blobThreads[index].threadYEndingPoint = blobList[index].yTop;

	}else{
		//deleteBlob(&blobList[index]);
	}
	pthread_mutex_unlock(&imageLock);
}//end function

int getLeft(int x, int y){

	while(x-1 >= 0 && differenceImagePixel2D[y][x - 1] == 255){
		x -= 1;
	}

	return x;
}

int getRight(int x, int y){

	while(x+1 < movieFrame.nbCols && differenceImagePixel2D[y][x + 1] == 255){
		x += 1;
	}

	return x;
}

void fill(int xL, int xR, int y, int index){

	addSegmentToBlob(&blobList[index],xL, xR, y);

	for (int k=xL; k<=xR + 1; k++){
	 differenceImagePixel2D[y][k] = 0x00;
	 blobList[index].xMax = findXMax(blobList[index].xMax, xR);
 	 blobList[index].xMin = findXMin(blobList[index].xMin, xL);
	}
}

int findFirstLeft(int xL, int xR, int y){
	//Find first left
	if(differenceImagePixel2D[y][xL] == 255){
		while(xL - 1 >= 0 && differenceImagePixel2D[y][xL - 1] == 255){
			xL -= 1;
		}//end while
	}else{
		while (xL + 1 <= movieFrame.nbCols-1 && differenceImagePixel2D[y][xL+1] == 0) {
			xL += 1;
		}//end while
	}//end if

	return xL;
	//end find first left
}

int findXMax(int xMax, int xR){
	if(xMax < xR){
		xMax = xR;
	}
	return xMax;
}

int findXMin(int xMin, int xL){
	if(xMin > xL){
		xMin = xL;
	}
	return xMin;
}

//==================================================================================
//	This is a part that you have to edit and add to, for example to
//	load a complete stack of images and initialize the output image
//	(right now it is initialized simply by reading an image into it.
//==================================================================================

void initializeApplication(void)
{
	//---------------------------------------------------------------
	//	I open a movie file
	//---------------------------------------------------------------
	libFile = lqt_open_read(MOVIE_PATH);
	if (libFile == NULL)
	{
		printf("video file not found at %s\n", MOVIE_PATH);
		exit(11);
	}
	if (!quicktime_video_tracks(libFile))
	{
		printf("video file not found at %s does not have any tracks\n", MOVIE_PATH);
		exit(12);
	}
	int cmodel=lqt_get_cmodel(libFile, TRACK);
	lqt_set_cmodel(libFile, TRACK, 7);
	if (!quicktime_reads_cmodel(libFile, cmodel, TRACK))
	{
		printf("Invalid cmodel of video (unrecognized format?)\n");
		exit(13);
	}

  frameIndex = 0;

	//---------------------------------------------------------------
	//	Get the dimensions of frames
	//---------------------------------------------------------------
	frameWidth = (unsigned int) quicktime_video_width(libFile,0);
	frameHeight = (unsigned int) quicktime_video_height(libFile,0);

	//---------------------------------------------------------------
	//	Allocate our ImageStruct at the proper dimensions
	//---------------------------------------------------------------
	numBlobs = 0;
	blobList = (Blob*) malloc(maxNumBlobs*sizeof(Blob));

	movieFrame = newImage(frameWidth, frameHeight, RGBA32_RASTER, 1);
	grayMovieFrame = newImage(frameWidth, frameHeight, GRAY_RASTER, 1);
	grayBackgroundImage = newImage(frameWidth,frameHeight,GRAY_RASTER,1);
	floatBackgroundImage = newImage(frameWidth, frameHeight, FLOAT_RASTER, 1);
	currentDifferenceImage = newImage(frameWidth, frameHeight, GRAY_RASTER, 1);


	//assign each pixel array to the apporiate frame
	movieFramePixel2D = (int**) movieFrame.raster2D;
	grayMovieFramePixel2D =(unsigned char**) grayMovieFrame.raster2D;
	grayBackgroundPixel2D = (unsigned char**) grayBackgroundImage.raster2D;
	floatBackgroundPixel2D = (float *) floatBackgroundImage.raster;
	differenceImagePixel2D = (unsigned char**) currentDifferenceImage.raster2D;

	//set the floatbackround to zero so that the frames can be summed with out errors
	for(int i = 0; i < floatBackgroundImage.nbRows; i++){
		for(int j = 0; j < floatBackgroundImage.nbCols; j++){
			floatBackgroundPixel2D[(i * floatBackgroundImage.nbCols) + j] = 0.0f;
		}
	}

	scaleX = (1.f*WINDOW_WIDTH)/frameWidth;
	scaleY = (1.f*WINDOW_HEIGHT)/frameHeight;


	initDone = 1;
	isPlaying = 1;
}


//---------------------------------------
//	Image utility functions.
//	Should be moved to some Image.c file
//---------------------------------------
ImageStruct newImage(unsigned int width, unsigned int height, ImageType type,
					 unsigned int wordSizeRowPadding)
{
	if (width<3 || height<3)
	{
		printf("Image size should be at least 3x3\n");
		exit(14);
	}
	if (wordSizeRowPadding!=1 && wordSizeRowPadding!=4 &&
		wordSizeRowPadding!=8 && wordSizeRowPadding!=16 &&
		wordSizeRowPadding!=32 && wordSizeRowPadding!=64)
	{
		printf("wordSizeRowPadding must be one of: 1, 4, 8, 16, 32, or 64\n");
		exit(15);
	}

	ImageStruct img;
	img.nbCols = width;
	img.nbRows = height;
	img.type = type;
	switch (type)
	{
		case RGBA32_RASTER:
		img.bytesPerPixel = 4;
		break;

		case GRAY_RASTER:
		img.bytesPerPixel = 1;
		break;

		case FLOAT_RASTER:
		img.bytesPerPixel = sizeof(float);
		break;
	}

	img.bytesPerRow = ((img.bytesPerPixel * width + wordSizeRowPadding - 1)/wordSizeRowPadding)*wordSizeRowPadding;
	img.raster = (void*) malloc(height*img.bytesPerRow);

	switch (type)
	{
		case RGBA32_RASTER:
		case GRAY_RASTER:
		{
			unsigned char* r1D = (unsigned char*) img.raster;
			unsigned char** r2D = (unsigned char**) calloc(height, sizeof(unsigned char*));
			img.raster2D = (void*) r2D;
			for (unsigned int i=0; i<height; i++)
				r2D[i] = r1D + i*img.bytesPerRow;
		}
		break;

		case FLOAT_RASTER:
		{
			float* r1D = (float*) img.raster;
			float** r2D = (float**) calloc(height, sizeof(float*));
			img.raster2D = (void*) r2D;
			for (unsigned int i=0; i<height; i++)
				r2D[i] = r1D + i*img.bytesPerRow;
		}
		break;
	}

	return img;
}
