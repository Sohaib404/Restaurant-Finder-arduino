/*
	Name: Sohaib Khadri, Oscar Gamelin
	ID: 1574054, 1570509
	CMPUT 275, Winter 2019

	Major Assignment 1 Part 2: Restaurant Finder
*/

#define SD_CS 10
#define JOY_VERT  A9 // should connect A9 to pin VRx
#define JOY_HORIZ A8 // should connect A8 to pin VRy
#define JOY_SEL   53

#include <Arduino.h>

// core graphics library (written by Adafruit)
#include <Adafruit_GFX.h>
// Hardware-specific graphics library for MCU Friend 3.5" TFT LCD shield
#include <MCUFRIEND_kbv.h>
// LCD and SD card will communicate using the Serial Peripheral Interface (SPI)
// e.g., SPI is used to display images stored on the SD card
#include <SPI.h>

// needed for reading/writing to SD card
#include <SD.h>
// Sd2Card class has methods for raw access 
// to SD and SDHC flash memory cards
Sd2Card card;

//needed for using touchsrceen
#include <TouchScreen.h>
// touch screen pins, obtained from the documentaion
#define YP A3 // must be an analog pin, use "An" notation!
#define XM A2 // must be an analog pin, use "An" notation!
#define YM 9  // can be a digital pin
#define XP 8  // can be a digital pin

// calibration data for the touch screen, obtained from documentation
// the minimum/maximum possible readings from the touch point
#define TS_MINX 100
#define TS_MINY 120
#define TS_MAXX 940
#define TS_MAXY 920

// thresholds to determine if there was a touch
#define MINPRESSURE   10
#define MAXPRESSURE 1000

// a multimeter reading says there are 300 ohms of resistance across the plate,
// so initialize with this to get more accurate readings
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);


#define REST_START_BLOCK 4000000    // address of the first restaurant data
#define NUM_RESTAURANTS 1066        // total number of restaurants

#include "lcd_image.h"

MCUFRIEND_kbv tft;

// width/height of the display when rotated horizontally and size of yeg image
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320
#define YEG_SIZE 2048


//Colour definitions
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

//  These  constants  are  for  the  2048 by 2048  map.
#define  MAP_WIDTH  2048
#define  MAP_HEIGHT  2048
#define  LAT_NORTH  5361858l
#define  LAT_SOUTH  5340953l
#define  LON_WEST  -11368652l
#define  LON_EAST  -11333496l

lcd_image_t yegImage = { "yeg-big.lcd", YEG_SIZE, YEG_SIZE };

//Definitions for the joystcik
#define JOY_CENTER   512
#define JOY_DEADZONE 64
#define CURSOR_SIZE 9

// the cursor position on the display
int cursorX, cursorY;
int reqRating = 1;
int rest_distLength;

int yegMiddleX = YEG_SIZE/2 - (DISPLAY_WIDTH - 60)/2;
int yegMiddleY = YEG_SIZE/2 - DISPLAY_HEIGHT/2;

#define NUM_RESTAURANTS 1066

//struct for reading in resaurant data
struct restaurant { // 64 bytes
  int32_t lat;
  int32_t lon;
  uint8_t rating;   // from 0 to 10
  char name[55];
};

//smaller struct for storing restaurant data
struct RestDist {
	uint16_t index ; // index of restaurant from 0 to NUM_RESTAURANTS -1
	uint16_t dist ; // Manhatten distance to cursor position
};
RestDist rest_dist[NUM_RESTAURANTS];

//  These  functions  convert  between x/y map  position  and  lat/lon
// (and  vice  versa.)
int32_t  x_to_lon(int16_t x) {
	return  map(x, 0, MAP_WIDTH , LON_WEST , LON_EAST);
}
int32_t  y_to_lat(int16_t y) {
	return  map(y, 0, MAP_HEIGHT , LAT_NORTH , LAT_SOUTH);
}
int16_t  lon_to_x(int32_t  lon) {
	return  map(lon , LON_WEST , LON_EAST , 0, MAP_WIDTH);
}
int16_t  lat_to_y(int32_t  lat) {
	return  map(lat , LAT_NORTH , LAT_SOUTH , 0, MAP_HEIGHT);
}

// global variable for assigning what algorithm to use
// 0 = qsort, 1 = isort, 2 = both
int sortMode = 0;


//swap function used by isort
void swap(RestDist *a, RestDist *b) {
	RestDist c = *a;
	*a = *b;
	*b = c;

}

//isort fuction for sorting restaurants based on distance from cursor
void isort(RestDist* restDistptr, uint16_t length) {
	int i = 1;
	while (i < length) {
		int j = i;
		while ((j > 0) && (((restDistptr[j-1]).dist) > ((restDistptr[j]).dist))) {
			swap(&restDistptr[j], &restDistptr[j-1]);
			j--;
		}
		i++;
	}
}

int pivot(RestDist* restDistptr, uint16_t n, uint16_t pi) {

  swap(&restDistptr[pi],&restDistptr[n-1]);

  int lo = 0;
  int hi = n-2;

  while(lo <= hi) {
    if (restDistptr[lo].dist <= restDistptr[n-1].dist) {
      lo++;
    } else if (restDistptr[hi].dist > restDistptr[n-1].dist) {
      hi--;
    } else {
      swap(&restDistptr[lo], &restDistptr[hi]);
    }
  }

  swap(&restDistptr[lo], &restDistptr[n-1]);

  return lo;

}

void qsort(RestDist* restDistptr, uint16_t n) {
  if (n <= 1) {
    return;
  }

  uint16_t pi = n/2;
  uint16_t new_pi = pivot(restDistptr, n, pi);

  qsort(restDistptr, new_pi);
  qsort(&restDistptr[new_pi], n - new_pi);
}

// variable storing previous block that was read
uint32_t prevBlockNum;
//function to read restaurant data from sd card
void getRestaurantFast(int restIndex, restaurant * restPtr) {
  // todo
	uint32_t blockNum = REST_START_BLOCK + restIndex/8;

  restaurant restBlock[8];

  //only read SD card if need to read from a new block
  if (blockNum != prevBlockNum) {
	//Serial.println("Reading SD");
	while (!card.readBlock(blockNum, (uint8_t*) restBlock)) {
	  Serial.println("Read block failed, trying again.");
	}
  }

  *restPtr = restBlock[restIndex % 8];
  prevBlockNum = blockNum;
}

void displayButtons(){
	if(reqRating > 5){
		reqRating = 1;
	}
	tft.fillRect(420, 0, 60, 320, TFT_BLACK);
  tft.drawRect(420, 0, 60, 160, TFT_BLUE);
  tft.drawRect(420, 160, 60, 160, TFT_BLUE);
  tft.setCursor(440, 75);
  tft.setTextSize(3);
  tft.print(reqRating);
  tft.setCursor(450, 235);
  tft.setTextSize(2);
  
  
  

  if (sortMode == 0) {
    tft.drawChar(440, 180, 'Q', WHITE, BLACK, 3);
    tft.drawChar(440, 205, 'S', WHITE, BLACK, 3);
    tft.drawChar(440, 230, 'O', WHITE, BLACK, 3);
    tft.drawChar(440, 255, 'R', WHITE, BLACK, 3);
    tft.drawChar(440, 280, 'T', WHITE, BLACK, 3);
  } else if (sortMode == 1) {
    tft.drawChar(440, 180, 'I', WHITE, BLACK, 3);
    tft.drawChar(440, 205, 'S', WHITE, BLACK, 3);
    tft.drawChar(440, 230, 'O', WHITE, BLACK, 3);
    tft.drawChar(440, 255, 'R', WHITE, BLACK, 3);
    tft.drawChar(440, 280, 'T', WHITE, BLACK, 3);
  } else if (sortMode == 2){
    tft.drawChar(440, 180, 'B', WHITE, BLACK, 3);
    tft.drawChar(440, 205, 'O', WHITE, BLACK, 3);
    tft.drawChar(440, 230, 'T', WHITE, BLACK, 3);
    tft.drawChar(440, 255, 'H', WHITE, BLACK, 3);
  }

}

// forward declaration for redrawing the cursor
void redrawCursor(uint16_t colour);

//setup function needed for arduinos
void setup() {
  init();

  Serial.begin(9600);

	pinMode(JOY_SEL, INPUT_PULLUP);


	//    tft.reset();             // hardware reset
  uint16_t ID = tft.readID();    // read ID from display
  Serial.print("ID = 0x");
  Serial.println(ID, HEX);
  if (ID == 0xD3D3) ID = 0x9481; // write-only shield
  
  // must come before SD.begin() ...
  tft.begin(ID);                 // LCD gets ready to work

	Serial.print("Initializing SD card...");
	if (!SD.begin(SD_CS)) {
		Serial.println("failed! Is it inserted properly?");
		while (true) {}
	}
	Serial.println("OK!");

	Serial.print("Initializing SPI communication for raw reads...");
	if (!card.init(SPI_HALF_SPEED, SD_CS)) {
		Serial.println("failed! Is the card inserted properly?");
		while (true) {}
	}
	Serial.println("OK!");

	tft.setRotation(1);

  tft.fillScreen(TFT_BLACK);

  // draws the centre of the Edmonton map, leaving the rightmost 60 columns black

	lcd_image_draw(&yegImage, &tft, yegMiddleX, yegMiddleY,
				 0, 0, DISPLAY_WIDTH - 60, DISPLAY_HEIGHT);
  displayButtons();

  // initial cursor position is the middle of the screen
  cursorX = (DISPLAY_WIDTH - 60)/2;
  cursorY = DISPLAY_HEIGHT/2;

  redrawCursor(TFT_RED);
}

void redrawCursor(uint16_t colour) {
  tft.fillRect(cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2,
			   CURSOR_SIZE, CURSOR_SIZE, colour);
}


//glogal variables for part of map to draw and drawing map function
//returns true if we had to redraw map which is used to check if we need to center cursor or constrain it
int16_t newICol = yegMiddleX;
int16_t newIRow = yegMiddleY;
bool redrawPatch(int16_t horiShift, int16_t vertShift) {

	newICol += horiShift;
	newIRow += vertShift;
	//return true
	bool draw = true;

	if (newICol == -(DISPLAY_WIDTH-60)) {
		newICol = 0;
		draw = false;
	} else if (newICol == YEG_SIZE) {
		newICol = YEG_SIZE-(DISPLAY_WIDTH-60);
		draw = false;
	} else if (newICol > YEG_SIZE-(DISPLAY_WIDTH-60)) {
		newICol = YEG_SIZE-(DISPLAY_WIDTH-60);
	} else if (newICol < 0) {
		newICol = 0;
	}

	if (newIRow == -(DISPLAY_HEIGHT)) {
		newIRow = 0;
		draw = false;
	} else if (newIRow == YEG_SIZE) {
		newIRow = (YEG_SIZE-DISPLAY_HEIGHT);
		draw = false;
	} else if (newIRow > (YEG_SIZE-DISPLAY_HEIGHT)) {
		newIRow = (YEG_SIZE-DISPLAY_HEIGHT);
	} else if (newIRow < 0) {
		newIRow = 0;
	}



	if (draw) {
		lcd_image_draw(&yegImage, &tft, newICol, newIRow,
		0, 0, (DISPLAY_WIDTH-60), DISPLAY_HEIGHT);
		return true;
	}


	return false;
}

// Will put restaurants of sufficient rating on the map
// Currently prints in the same position and is called from mode1-->mode0 switch
// Not called from button
void drawRests(){
  
  restaurant rDots;
	   int16_t xPos;
	   int16_t yPos;
	   int16_t showRest;
	   for (int i = 0; i < NUM_RESTAURANTS; ++i) {
	   getRestaurantFast(i, &rDots);
			
			xPos = lon_to_x(rDots.lon) - newICol;
			yPos = lat_to_y(rDots.lat) - newIRow;
			if(((rDots.rating + 1)/2) == reqRating || ((rDots.rating + 1)/2) > reqRating){
			  
			  if((xPos > 0) && (xPos < (DISPLAY_WIDTH-60) - 4) && (yPos > 0) && (yPos < DISPLAY_HEIGHT)) {
			  	tft.fillCircle(xPos, yPos, 3.5, BLUE);
				}
			  
			}
			else{
				showRest = 0;
			}

			//draw circles on restaurants within map display
			if((xPos > 0) && (xPos < (DISPLAY_WIDTH-60) - 4) && (yPos > 0) && (yPos < DISPLAY_HEIGHT) && (showRest == 1)) {
			  tft.fillCircle(xPos, yPos, 3.5, BLUE);
			}
		}
} 



//variables used for timing algorithms
uint32_t qStart;
uint32_t qEnd;
uint32_t qTime;
uint32_t iStart;
uint32_t iEnd;
uint32_t iTime;

//function that reads restaurant data by index and calls to specified algorithm to sort it
void sortRest() {

		restaurant rSorting;
		int j = 0;
		int16_t xDist;
		int16_t yDist;
		for (int i = 0; i < NUM_RESTAURANTS; ++i) {
			getRestaurantFast(i, &rSorting);
			if(((rSorting.rating + 1)/2) == reqRating || ((rSorting.rating + 1)/2) > reqRating){
				rest_dist[j].index = i;
				xDist = (newICol + cursorX) - lon_to_x(rSorting.lon);
				yDist = (newIRow + cursorY) - lat_to_y(rSorting.lat);
				rest_dist[j].dist = abs(xDist) + abs(yDist);
				rest_distLength = j;
				j++;
			}
		}
		
    if (sortMode == 0) {

      Serial.print("Quick Sort running time: ");
      qStart = millis();
      qsort(rest_dist, j);
      qEnd = millis();
      qTime = qEnd - qStart;
      Serial.print(qTime);
      Serial.println(" ms");

    } else if (sortMode == 1) {

      Serial.print("Insertion Sort running time: ");
      iStart = millis();
      isort(rest_dist, j);
      iEnd = millis();
      iTime = iEnd - iStart;
      Serial.print(iTime);
      Serial.println(" ms");

    } else if (sortMode == 2) {

      Serial.print("Quick Sort running time: ");
      qStart = millis();
      qsort(rest_dist, j);
      qEnd = millis();
      qTime = qEnd - qStart;
      Serial.print(qTime);
      Serial.println(" ms");

      restaurant rSorting;
      int j = 0;
      int16_t xDist;
      int16_t yDist;
      for (int i = 0; i < NUM_RESTAURANTS; ++i) {
        getRestaurantFast(i, &rSorting);
        if(((rSorting.rating + 1)/2) == reqRating || ((rSorting.rating + 1)/2) > reqRating){
          rest_dist[j].index = i;
          xDist = (newICol + cursorX) - lon_to_x(rSorting.lon);
          yDist = (newIRow + cursorY) - lat_to_y(rSorting.lat);
          rest_dist[j].dist = abs(xDist) + abs(yDist);
          rest_distLength = j;
          j++;
        }
      }

      Serial.print("Insertion Sort running time: ");
      iStart = millis();
      isort(rest_dist, j);
      iEnd = millis();
      iTime = iEnd - iStart;
      Serial.print(iTime);
      Serial.println(" ms");

    }

}

//dehighlights previosly selected restaurant in mode 1
void dehighlight(int selectedRest) {

	restaurant rDehighlight;
	getRestaurantFast(rest_dist[selectedRest].index , &rDehighlight);

	tft.setCursor(0, 15*(selectedRest % 21));
	tft.setTextColor (0xFFFF , 0x0000);
	tft.setTextSize(2);
	tft.print(rDehighlight.name);
	tft.print("\n");

}

//highlights selected restaurant in mode 1
void highlight(int selectedRest) {

	restaurant rHighlight;
	getRestaurantFast(rest_dist[selectedRest].index , &rHighlight);

	tft.setCursor(0, 15*(selectedRest % 21));
	tft.setTextColor (0x0000 , 0xFFFF);
	tft.setTextSize(2);
	tft.print(rHighlight.name);
	tft.print("\n");
	 
}

void list(int startRest) {
	int i = 0;
  while(i < 21) {
  tft.setCursor(0, 15*i);
  restaurant r;
  if(rest_dist[i + startRest].index != NULL){
	  getRestaurantFast(rest_dist[startRest + i].index , &r);
	  if(((r.rating + 1)/2) == reqRating || ((r.rating + 1)/2) > reqRating){
		  if (i !=  0) { // not  highlighted
			//  white  characters  on  black  background
			tft.setTextColor (0xFFFF , 0x0000);
		  } 
		  else { //  highlighted
			//  black  characters  on  white  background
			tft.setTextColor (0x0000 , 0xFFFF);
		  }
		  tft.setTextSize(2);
		  tft.print(r.name);
		  tft.print("\n");
			
		  }
		  //Serial.println(startRest);
		  tft.print("\n");
		  
		}
		i++;
	}
}

//displays selected restaurant chosen from mode1 into mode0 and centers cursor on it
void chooseRest(int16_t xRest, int16_t yRest) {

	if (xRest < (DISPLAY_WIDTH-60)/2) {
		newICol = 0;
	} else if (xRest > YEG_SIZE - (DISPLAY_WIDTH-60)/2) {
		newICol = YEG_SIZE - 420;
	} else {
		newICol = xRest - (DISPLAY_WIDTH-60)/2;
	}

	if (yRest < (DISPLAY_HEIGHT)/2) {
		newIRow = 0;
	} else if (yRest > YEG_SIZE - (DISPLAY_HEIGHT)/2) {
		newICol = YEG_SIZE - DISPLAY_HEIGHT;
	} else {
		newIRow = yRest - DISPLAY_HEIGHT/2;
	}


	lcd_image_draw(&yegImage, &tft, newICol, newIRow,
				 0, 0, DISPLAY_WIDTH - 60, DISPLAY_HEIGHT);

	if (xRest < 0) {
		cursorX = CURSOR_SIZE/2;
	} else if (xRest > (DISPLAY_WIDTH-60)/2 && (xRest < YEG_SIZE - (DISPLAY_WIDTH-60))) {
		cursorX = (DISPLAY_WIDTH - 60)/2;
	} else if (xRest > YEG_SIZE) {
		cursorX = YEG_SIZE - CURSOR_SIZE/2;
	} else {
		  cursorX = xRest;
	}

	if (yRest < 0) {
		cursorY = CURSOR_SIZE/2;
	} else if (yRest > (DISPLAY_HEIGHT)/2 && (yRest < YEG_SIZE - (DISPLAY_HEIGHT))) {
		cursorY = DISPLAY_HEIGHT/2;
	} else if (yRest > YEG_SIZE){
		cursorY = YEG_SIZE - CURSOR_SIZE/2;
	} else {
		cursorY = yRest;
	}
	redrawCursor(TFT_RED);
	displayButtons();
}

//mode1 function that calls to list restaurants, select restaurant, and display chosen restaurant in mode0
//delay used for smooth scrolling
void mode1 () {

		tft.fillScreen (0);
		tft.setCursor(0, 0); //  where  the  characters  will be  displayed
		tft.setTextWrap(false);
		tft.setTextSize(2);
		int selectedRest = 0; //  which  restaurant  is  selected?

		sortRest();


		list(0);
	  while(digitalRead(JOY_SEL) != 0) {

		if ((analogRead(JOY_VERT) > JOY_CENTER + JOY_DEADZONE) && (selectedRest < rest_distLength)){
				  selectedRest++;
				  delay(20);
				highlight(selectedRest);
			  delay(20);
			  dehighlight(selectedRest-1);
			  //Serial.println(selectedRest);
		  if(selectedRest % 21 == 0 && selectedRest != 0){
			tft.fillScreen (0);
			list(selectedRest);
			//Serial.println(selectedRest);
		  }
		  //Serial.println(selectedRest);
			   
		} else if (analogRead(JOY_VERT) < JOY_CENTER - JOY_DEADZONE) {
			
				selectedRest--;
				if(selectedRest < 0){
		 			selectedRest = 0;
		 		}
				delay(20);
				highlight(selectedRest);
				delay(20);
				dehighlight(selectedRest+1);
			if(selectedRest % 21 == 20){
			  tft.fillScreen (0);
			  list(selectedRest - 20);
			  selectedRest = selectedRest - 20;
			  //Serial.println(selectedRest);

			}
			//Serial.println(selectedRest);
		 }
		}
	  

  restaurant rChoose;
  getRestaurantFast(rest_dist[selectedRest].index , &rChoose);
  uint16_t xRest = lon_to_x(rChoose.lon);
  uint16_t yRest = lat_to_y(rChoose.lat);

  tft.fillScreen(0);
  chooseRest(xRest, yRest);  
}

//function that processes touchscreen and marks restaurants within display with a blue circle and triggers buttons
void processTouchscreen() {

	//get touch input 
	TSPoint touch = ts.getPoint();
	// restore pinMode to output after reading the touch
	// this is necessary to talk to tft display
	pinMode(YP, OUTPUT); 
	pinMode(XM, OUTPUT); 

	//check for touch
	if (touch.z > MINPRESSURE && touch.z < MAXPRESSURE) {
	   int16_t screen_x = map(touch.y, TS_MINX, TS_MAXX, DISPLAY_WIDTH-1, 0);
	   int16_t screen_y = map(touch.x, TS_MINY, TS_MAXY, DISPLAY_HEIGHT-1, 0);

		if(screen_x < 420){
	  	drawRests();
	  }
	  else if(screen_y < 180){
	  	reqRating++;
	  	displayButtons();
	  }
	  else{
      sortMode++;
      if(sortMode > 2) {
        sortMode = 0;
      }
      displayButtons();
	  	
	  }
    delay(250);
	}
}
   

//function that processes joystick controls
void processJoystick() {
	int xVal = analogRead(JOY_HORIZ);
	int yVal = analogRead(JOY_VERT);
	int buttonVal = digitalRead(JOY_SEL);

	//horizontal and vertical speed multipliers
	int xSpeed = abs(xVal - JOY_CENTER) / 20;
	int ySpeed = abs(yVal - JOY_CENTER) / 20;

	// now move the cursor
	if ((yVal < JOY_CENTER - JOY_DEADZONE)) {
	//draw map patch at cursor location before moving cursor
		lcd_image_draw(&yegImage, &tft, newICol + cursorX - CURSOR_SIZE/2, newIRow + cursorY - CURSOR_SIZE/2,
		cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2, CURSOR_SIZE, CURSOR_SIZE);
		cursorY -= 1*ySpeed; // decrease the y coordinate of the cursor

	//cursorY constrain
	if (cursorY < CURSOR_SIZE/2) {
		//shift map up
		if (redrawPatch(0, -DISPLAY_HEIGHT)) {
			cursorX = (DISPLAY_WIDTH - 60)/2;
			cursorY = DISPLAY_HEIGHT/2;
		} else {
			cursorY = CURSOR_SIZE/2;
		}
		
	}
	redrawCursor(TFT_RED);

  }	else if ((yVal > JOY_CENTER + JOY_DEADZONE)) {
	//draw map patch at cursor location before moving cursor
	lcd_image_draw(&yegImage, &tft, newICol + cursorX - CURSOR_SIZE/2, newIRow + cursorY - CURSOR_SIZE/2,
	cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2, CURSOR_SIZE, CURSOR_SIZE);

	cursorY += 1*ySpeed;

	//cursorY constrain
	if (cursorY > DISPLAY_HEIGHT - CURSOR_SIZE/2) {

		//shift map down
		if (redrawPatch(0, DISPLAY_HEIGHT)) {
			cursorX = (DISPLAY_WIDTH - 60)/2;
			cursorY = DISPLAY_HEIGHT/2;
		} else {
			cursorY = DISPLAY_HEIGHT - CURSOR_SIZE/2;
		}
		
	}
	redrawCursor(TFT_RED);
  }

  // remember the x-reading increases as we push left
  if ((xVal > JOY_CENTER + JOY_DEADZONE)) {
	//draw map patch at cursor location before moving cursor
	lcd_image_draw(&yegImage, &tft, newICol + cursorX - CURSOR_SIZE/2, newIRow + cursorY - CURSOR_SIZE/2,
	cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2, CURSOR_SIZE, CURSOR_SIZE);

	cursorX -= 1*xSpeed;

	//cursorX constrain
	if (cursorX < CURSOR_SIZE/2) {

		//shift map left
		if (redrawPatch(-(DISPLAY_WIDTH-60), 0)) {
			cursorX = (DISPLAY_WIDTH - 60)/2;
			cursorY = DISPLAY_HEIGHT/2;
		} else {
			cursorX = CURSOR_SIZE/2;
		}


	}
	redrawCursor(TFT_RED);

  } else if ((xVal < JOY_CENTER - JOY_DEADZONE)) {
	//draw map patch at cursor location before moving cursor
	lcd_image_draw(&yegImage, &tft, newICol + cursorX - CURSOR_SIZE/2, newIRow + cursorY - CURSOR_SIZE/2,
	cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2, CURSOR_SIZE, CURSOR_SIZE);
	cursorX += 1*xSpeed;

	//cursorX constrain
	if (cursorX > (DISPLAY_WIDTH-61) - CURSOR_SIZE/2) {

		//shift map right
		if (redrawPatch(DISPLAY_WIDTH-60, 0)) {
			cursorX = (DISPLAY_WIDTH - 60)/2;
			cursorY = DISPLAY_HEIGHT/2;
		} else {
			cursorX = (DISPLAY_WIDTH-61) - CURSOR_SIZE/2;
		}

	}
	redrawCursor(TFT_RED);
  }

	//check if joystick pressed to switch to mode1
	if(buttonVal == 0) {
		mode1();
	}
 
	delay(20);
}



//main function continously check for joystick and touchscreen input
int main() {
	setup();

	while (true) {
		processJoystick();
		processTouchscreen();
	}

	Serial.end();
	return 0;
}
