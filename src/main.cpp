#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <stack>
#include <queue>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <png.h>

#include "resource.h"
#include "global.h"
#include "Level.h"
#include "Image.h"
#include "blocks.h"

using namespace std;

class dirlist {
private:
  queue<string> directories;
  queue<string> files;

public:
  dirlist(string path) {
    directories.push(path);
  }
  
  bool hasnext() {
    if (!files.empty()) {
      return true;
    }

    if (directories.empty()) {
      return false;
    }
    
    // work until you find any files
    while (!directories.empty()) {
      string path = directories.front();
      directories.pop();
      
      DIR *dir = opendir(path.c_str()); 
      
      if (!dir) {
        return false;
      }
      
      dirent *ent; 
      
      while((ent = readdir(dir)) != NULL)
      {
        string temp_str = ent->d_name;

        if (temp_str.compare(".") == 0) {
          continue;
        }
        
        if (temp_str.compare("..") == 0) {
          continue;
        }
        
        if (ent->d_type == DT_DIR) {
          directories.push(path + "/" + temp_str);
        }
        else if (ent->d_type == DT_REG) {
          files.push(path + "/" + temp_str);
        }
      }
      
      closedir(dir);
    }
    
    return !files.empty();
  }

  string next() {
    string next = files.front();
    files.pop();
    return next;
  }
};

int write_image(settings_t *s, const char *filename, Image &img, const char *title)
{
   int code = 0;
   FILE *fp;
   png_structp png_ptr = NULL;
   png_infop info_ptr = NULL;
   png_bytep row = NULL;

   fp = fopen(filename, "wb");

   if (fp == NULL) {
      code = 1;
      goto finalise;
   }

   png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if (png_ptr == NULL) {
      code = 1;
      goto finalise;
   }

   info_ptr = png_create_info_struct(png_ptr);

   if (info_ptr == NULL) {
      code = 1;
      goto finalise;
   }

   if (setjmp(png_jmpbuf(png_ptr))) {
      code = 1;
      goto finalise;
   }

   png_init_io(png_ptr, fp);

   png_set_IHDR(png_ptr, info_ptr, img.get_width(), img.get_height(),
         8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    
   if (title != NULL) {
      png_text title_text;
      title_text.compression = PNG_TEXT_COMPRESSION_NONE;
      title_text.key = (char *)"Title";
      title_text.text = (char *)title;
      png_set_text(png_ptr, info_ptr, &title_text, 1);
   }

   png_write_info(png_ptr, info_ptr);

   row = (png_bytep) malloc(4 * img.get_width() * sizeof(png_byte));

   int x, y;
   for (y=0 ; y<img.get_width(); y++) {
      for (x=0 ; x<img.get_width(); x++) {
        Color c = img.get_pixel(x, y);
        row[0 + x*4] = c.r;
        row[1 + x*4] = c.g;
        row[2 + x*4] = c.b;
        row[3 + x*4] = 0xff;
      }

      png_write_row(png_ptr, row);
   }
  
   png_write_end(png_ptr, NULL);

finalise:
   if (fp != NULL) {
     fclose(fp);
   }
    
   if (info_ptr != NULL) {
     png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
   }

   if (png_ptr != NULL) {
     png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
   }

   if (row != NULL) {
     free(row);
   }

   return code;
}

/*
 * Store part of a level rendered as a small image.
 *
 * This will allow us to composite the entire image later and calculate sizes then.
 */
struct partial {
  int xPos;
  int zPos;
  Image *image;
};

void do_world(settings_t *s, string world, string output) {
  if (!s->silent) {
    cout << "world:  " << world << " " << endl;
    cout << "output: " << output << " " << endl;
    cout << endl;
  }
  
  if (!s->silent) cout << "Reading and projecting blocks ... " << endl;
  
  queue<partial> partials;
  
  dirlist listing(world);

  int i = 1;

  int minx = INT_MAX;
  int minz = INT_MAX;
  int maxx = INT_MIN;
  int maxz = INT_MIN;

  while (listing.hasnext()) {
    string path = listing.next();
    Level level(path.c_str());

    if (level.xPos < minx) {
      minx = level.xPos;
    }

    if (level.xPos > maxx) {
      maxx = level.xPos;
    }

    if (level.zPos < minz) {
      minz = level.zPos;
    }

    if (level.zPos > maxz) {
      maxz = level.zPos;
    }
    
    partial p;
    p.image = level.get_image();
    p.xPos = level.xPos;
    p.zPos = level.zPos;
    partials.push(p);
    
    if (!s->silent) {
      if (i % 100 == 0) {
        cout << " " << setw(10) << i << flush;
      }
        
      if (i % 800 == 0) {
        cout << endl;
      }
        
      ++i;
    }
  }
  
  if (!s->silent) cout << " done!" << endl;
  if (!s->silent) cout << "Compositioning image... " << flush;
  
  int diffx = maxx - minx;
  int diffz = maxz - minz;
  int image_width = diffx * 16 + 16;
  int image_height = diffz * 16 + 16;
  size_t approx_memory = image_width * image_height * sizeof(nbt::Byte) * 4;
  
  if (!s->silent) cout << "png will be " << image_width << "x" << image_height << " and required approx. "
       << approx_memory << " bytes of memory ... " << flush;
  
  Image all(image_width, image_height);
  
  while (!partials.empty()) {
    partial p = partials.front();
    partials.pop();
    int xoffset = (p.xPos - minx) * 16;
    int yoffset = (p.zPos - minz) * 16;
    all.composite(xoffset, yoffset, *p.image);
    delete p.image;
  }
  
  if (!s->silent) cout << "done!" << endl;
  if (!s->silent) cout << "Saving image to " << output << "... " << flush;
  
  if (write_image(s, output.c_str(), all, "Map generated by c10t") != 0) {
    cerr << "failed! " << strerror(errno) << endl;
    exit(1);
  }
  
  if (!s->silent) cout << "done!" << endl;
}

void do_help() {
  cout << "This program was made possible by the inspiration and work of ZomBuster and Firemark" << endl;
  cout << "Written by Udoprog" << endl;
  cout << endl;
  cout << "Usage: c10t [options]" << endl;
  cout << "Options:" << endl
    << "  -w <world-directory> : use this world directory as input" << endl
    << "  -o <outputfile> : use this file as output file for generated png" << endl
    << "  -s : execute silently, printing nothing except errors" << endl;
  cout << endl;
  cout << "Typical usage:" << endl;
  cout << "   c10t -w /path/to/world -o /path/to/png.png" << endl;
  cout << endl;
}

int do_colors() {
  cout << "List of material Colors (total: " << mc::MaterialCount << ")" << endl;
  
  for (int i = 0; i < mc::MaterialCount; i++) {
    cout << i << ": " << mc::MaterialName[i] << " = " << mc::MaterialColor[i]->to_s() << endl;
  }
  
  return 0;
}

int main(int argc, char *argv[]){
  mc::initialize_constants();

  settings_t *s = new settings_t();
  
  string world;
  string output;
  int c;
  
  while ((c = getopt (argc, argv, "shw:o:")) != -1)
  {
    switch (c)
    {
    case 'w': world = optarg; break;
    case 'o': output = optarg; break;
    case 's': s->silent = true; break;
    case 'h':
      do_help();
      return 0;
    case '?':
      if (optopt == 'c')
        fprintf (stderr, "Option -%c requires an argument.\n", optopt);
      else if (isprint (optopt))
        fprintf (stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf (stderr,
              "Unknown option character `\\x%x'.\n",
              optopt);
       return 1;
     default:
       abort ();
     }
  }
  
  if (!s->silent) {
    cout << "type '-h' for help" << endl;
    cout << endl;
  }
  
  if (!world.empty()) {
    if (output.empty()) {
      cout << "error: You must specify output file using '-o' to generate world" << endl;
      cout << endl;
      do_help();
      return 1;
    }

    do_world(s, world, output);
  } else {
    do_help();
    return 1;
  }
  
  return 0;
};
