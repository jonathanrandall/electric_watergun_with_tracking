/** 
 *  See https://www.youtube.com/watch?v=NIbiG6at01g&t=159s
 *  Original code ideas from https://bitbucket.org/makersmashup/directional-motion-detector/src/master/
 */

#ifndef helpers_h
#define helpers_h



//#define WIDTH 640 //320                 // Resolution Width
//#define HEIGHT 480 //240                // Resolution height
#define BLOCK_SIZE 4    
#define BL_N 8
//#define MY_FRAMESIZE FRAMESIZE_VGA

#define WIDTH 320//640 //320                 // Resolution Width
#define HEIGHT 240//480 //240                // Resolution height
#define MY_FRAMESIZE FRAMESIZE_QVGA
//#define BLOCK_SIZE 4//8 //4              // Size of each sensor block on the display (reduced granularity for speed)
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 20//1.5

#define IMAGE_DIFF_THRESHOLD 26 //0.1
#define IMAGE_DIFF_THRESHOLD1 0.03
#define DEBUG 0                            // Good for making changes
#define INFO 0                             // Good to see whats happening (turn off to save CPU)
#define VIEWPORT_PIXELS WIDTH / BLOCK_SIZE // 320/4 = 80 Positions to look at in left to right view plane [-------X------] X is where motion exists


#define FLASH_PIN 4                        // Pin of ESP32 Flash (Led)
#define FLASH_MODE 0                       // 0 = 0ff , 1 = flash, 2 = steady

bool do_tracking = false;
bool finished_tracking = true;
bool first_capture = true;
uint8_t cnt = 0;
bool fire_waterpistol;
unsigned long watergun_off_time = 0;

camera_fb_t * fb;
uint16_t prev_frame[H][W] = {0};
uint16_t current_frame[H][W] = {0};
//uint16_t current_frame_last[H][W] = {0};
uint16_t empty_frame[H][W] = {0};
long motionView[VIEWPORT_PIXELS];
int region_of_interest = 5;
int region_of_interest_y; //track in the y direction (TODO)

int viewPortToRegion(long mv[]);
bool capture_still();
bool motion_detect();
void update_frame();
void print_frame(uint16_t frame[H][W]);


/**
 * Capture image and do down-sampling
 */
bool capture_still()
{

    memcpy(empty_frame, current_frame, sizeof(empty_frame)); // FAST! Memcopy vs iterations so much faster.
    

    // down-sample image in blocks
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++)
    {
        const uint16_t x = i % WIDTH;
        const uint16_t y = floor(i / WIDTH); //WIDTH
        const uint8_t block_x = floor(x / BLOCK_SIZE);
        const uint8_t block_y = floor(y / BLOCK_SIZE);
        const uint8_t pixel = fb->buf[i];
        const uint16_t current = current_frame[block_y][block_x];
        // average pixels in block (accumulate)
        current_frame[block_y][block_x] += pixel;
    }

    // average pixels in block (rescale)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++){
            current_frame[y][x] /= BLOCK_SIZE * BLOCK_SIZE;
           }


//    esp_camera_fb_return(frame_buffer); // Needed to free up camera memory

    return true;
}

/**
 * Compute the number of different blocks
 * If there are enough, then motion happened
 */
bool motion_detect()
{
    uint16_t changes = 0;
    int lastBlock = 0;
    const uint16_t blocks = (WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE);
    for (uint16_t y = 0; y < H; y++)
    {
        for (uint16_t x = 0; x < W; x++)
        {
            float current = (float) current_frame[y][x];
            float prev = (float) prev_frame[y][x];
            float delta = abs(current - prev) ;/// prev;

            // Fill only those areas that meet the threashold.
            if (delta >= BLOCK_DIFF_THRESHOLD)
            {
#if DEBUG
                Serial.println(delta);
                Serial.print("diff\t");
                Serial.print(y);
                Serial.print('\t');
                Serial.println(x);
#endif
//                if (delta >=50) delta = 50;

                motionView[x] += 1;
                changes++;
            }
        }
    }
    if (changes == 0)
        return false; // don't need to go any further

    // Change screen data into linear (left to right) expression of data.
    region_of_interest = viewPortToRegion(motionView);
    
    
// Display updates for informational purposes.
#if INFO
    Serial.print(":::");
#endif
Serial.println("motion veiw");
    // Clear viewport to zero for next detection phase
    for (uint16_t i = 0; i < VIEWPORT_PIXELS; i++)
    {
      
//#if INFO
        Serial.print(motionView[i]);
        
//#endif
        motionView[i] = 0;
    }
    Serial.println("");

//    Serial.println(":::");
//    Serial.print("Changed ");
//    Serial.print(changes);
//    Serial.print(" out of ");
//    Serial.println(blocks);

//    Serial.print("MoveTo:");
//    Serial.println(moveTo);

    Serial.println((1.0 * changes / blocks));
    fire_waterpistol = (1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD1;
    return (region_of_interest >= 0);
    
//    return ((1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD1) && (region_of_interest >= 0);
}

/**
 * Copy current frame to previous
 */
void update_frame()
{
    memcpy(prev_frame, current_frame, sizeof(prev_frame)); // FAST! Memcopy vs iterations so much faster.
}


int viewPortToRegion(long mv[])
{
    int maxVal = 0;
    int region = -1;
    int region_wide = -1;
    int tmpVal = 0;
    bool qualify = false;
//    char str_tmp[9];

    
    
    // Fill each char arry with the 8 bits of the 10 regions
    for (int i = 0; i < 3; i++)
    {
//      if(i==0)
        for (int j = 0; j < 26; j++)
        {
            tmpVal += (mv[((i * 26) + j)+1]);// == 1) ? '1' : '0';
//            tmpVal += (mv[j]);// == 1) ? '1' : '0';
            //  Serial.println(((i*8)+j));
        }
        /*
      if(i==1){
        for (int j = 0; j < 20; j++)
          {
              tmpVal += (mv[(30 + j)]);// == 1) ? '1' : '0';
              //  Serial.println(((i*8)+j));
          }
          tmpVal = (int) 1.5*((float) tmpVal);
      }
      if(i==2)
        for (int j = 0; j < 30; j++)
        {
            tmpVal += (mv[j+50]);// == 1) ? '1' : '0';
            //  Serial.println(((i*8)+j));
        }
        */
//        tmpVal = readBinaryString(str_tmp);
#if INFO
        Serial.print("Block: ");
        Serial.print(i);
        
        Serial.print(" Value: ");
        Serial.println(tmpVal);
#endif
        if (tmpVal > maxVal)
        {
            maxVal = tmpVal; // Set new uppper mark
            region_wide = i;      // Which viewport has the most movement.
        }
        tmpVal = 0;
    }
    qualify =(maxVal > IMAGE_DIFF_THRESHOLD);
    maxVal = 0;
    tmpVal = 0;
    //break regions down
    if(region_wide == 0) //search left
    for (int i = 0; i<3; i++){
      for (int j = 0; j < 9; j++)
          {
              tmpVal += (mv[((i * 9) + j)]);
              //  Serial.println(((i*8)+j));
          } 
      if (tmpVal > maxVal)
        {
            maxVal = tmpVal; // Set new uppper mark
            region = i;      // Which viewport has the most movement.
        }
        tmpVal = 0;
    }

    if(region_wide == 1) //search middle
    for (int i = 3; i<6; i++){
      for (int j = 0; j < 9; j++)
          {
              tmpVal += (mv[((i * 9) + j)-1]);
              //  Serial.println(((i*8)+j));
          } 
      if (tmpVal > maxVal)
        {
            maxVal = tmpVal; // Set new uppper mark
            region = i;      // Which viewport has the most movement.
        }
        tmpVal = 0;
    }

    if(region_wide == 2) //search right
    for (int i = 6; i<9; i++){
      for (int j = 0; j < 9; j++)
          {
              tmpVal += (mv[((i * 9) + j)-1]);
              //  Serial.println(((i*8)+j));
          } 
      if (tmpVal > maxVal)
        {
            maxVal = tmpVal; // Set new uppper mark
            region = i;      // Which viewport has the most movement.
        }
        tmpVal = 0;
    }
//    Serial.print("Most activity in block region:");
//    Serial.println(region);
    if(qualify)  return region;
    return -1;
}

void print_frame(uint16_t frame[H][W])
{
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            Serial.print(frame[y][x]);
            Serial.print('\t');
        }

        Serial.println();
    }
}


#endif
