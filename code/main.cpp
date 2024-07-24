#include <SDL2/SDL.h>

// God bless Sean Barrett and his C libraries.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TICKS_PER_FRAME (1000 / 60)
#define MAX_SATELLITES  1000
#define MAX_GUYS        1000
#define MAX_EXPLOSIONS  10
#define SATELLITE_SIZE  64
#define EXPLOSION_SIZE  128
#define GUY_WIDTH       7
#define GUY_HEIGHT      14
#define EXPLOSION_TIME  1000

typedef unsigned char uchar;
typedef unsigned long ulong;

struct Image {
  uchar* data;
  int    width;
  int    height;
  int    channels;

  int pitch() {
    return width * channels;
  }
};

static Image load_image(const char* path) {
  Image image = {};
  image.data  = stbi_load(path, &image.width, &image.height, &image.channels, 4);
  return image;
}

struct Vector2 {
  float x, y;
};

static Vector2 operator+(Vector2 a, float b) {
  return (Vector2) { a.x + b, a.y + b };
}

static Vector2 operator-(Vector2 a, Vector2 b) {
  return (Vector2) { a.x - b.x, a.y - b.y };
}

static Vector2 operator/(Vector2 a, float b) {
  return (Vector2) { a.x / b, a.y / b };
}

static void operator+=(Vector2& a, Vector2 b) {
  a.x += b.x;
  a.y += b.y;
}

static void operator-=(Vector2& a, float b) {
  a.x -= b;
  a.y -= b;
}

static void operator*=(Vector2& a, float b) {
  a.x *= b;
  a.y *= b;
}

static float square_length(Vector2 a) {
  return a.x * a.x + a.y * a.y;
}

static int max(int a, int b) {
  return a > b ? a : b;
}

static bool  started;
static float planet_rotation;

static Vector2 satellite_positions [MAX_SATELLITES];
static Vector2 satellite_velocities[MAX_SATELLITES];
static int     satellite_to_guy    [MAX_SATELLITES];
static int     satellite_count;

static Vector2 explosion_positions[MAX_EXPLOSIONS];
static int     explosion_end_ticks[MAX_EXPLOSIONS];
static int     explosion_frame    [MAX_EXPLOSIONS];
static int     explosion_count;

typedef enum {
  DEPLOYING_NULL,
  DEPLOYING_START,
  DEPLOYING_END,
} Deploying;

static Deploying deploying;
static Sint32    deploy_start[2];
static Sint32    deploy_end[2];

static float guy_angles[MAX_GUYS];
static float guy_magnitudes[MAX_GUYS];
static int   guy_count;

static ulong guy_picked[MAX_GUYS / 64];
static int   guy_picked_count;

static int ticks_per_guy = 5000;

static int score;

static SDL_Rect planet_sprite    = {  0,  0, 64, 64 };
static SDL_Rect satellite_sprite = { 64,  0, 16, 16 };
static SDL_Rect guy_sprite       = { 64, 16,  7, 14 };
static SDL_Rect a_sprite         = {  0, 64, 16, 16 };
static SDL_Rect zero_sprite      = {  0, 80, 16, 16 };
static SDL_Rect explosion_sprite = { 96,  0, 16, 16 };

static float to_radians(float degrees) {
  return degrees / 180 * M_PI;
}

static void delete_satellite(int i) {
  int end = satellite_count - 1;
  satellite_positions [i] = satellite_positions [end];
  satellite_velocities[i] = satellite_velocities[end];
  satellite_to_guy    [i] = satellite_to_guy    [end];
  satellite_count--;
}

static void delete_explosion(int i) {
  int end = explosion_count - 1;
  explosion_positions[i] = explosion_positions[end];
  explosion_end_ticks[i] = explosion_end_ticks[end];
  explosion_count--;
}

static Vector2 get_guy_position(int i, int window_width, int window_height) {
  float planet_radius = fmin(window_width / 4, window_height / 4);
  float guy_angle     = guy_angles[i] + to_radians(planet_rotation);
  
  Vector2 position;
  position.x = planet_radius * guy_magnitudes[i] * cos(guy_angle) + window_width  / 2;
  position.y = planet_radius * guy_magnitudes[i] * sin(guy_angle) + window_height / 2;
  return position;
}

static float draw_text(SDL_Renderer* renderer, SDL_Texture* texture, int x, int y, const char* text) {
  SDL_Rect destination = { x, y, 32, 32 };
  for (int i = 0; text[i]; i++) {
    char     c      = text[i];
    bool     draw   = true;
    SDL_Rect source = {};

    if ('a' <= c && c <= 'z') {
      source    = a_sprite;
      source.x += (c - 'a') * 16;
    } else if ('A' <= c && c <= 'Z') {
      source    = a_sprite;
      source.x += (c - 'A') * 16;
    } else if ('0' <= c && c <= '9') {
      source    = zero_sprite;
      source.x += (c - '0') * 16;
    } else {
      draw = false;
    }

    if (draw) {
      int result = SDL_RenderCopy(renderer, texture, &source, &destination);
      SDL_assert(result == 0);
    }
      
    destination.x += 32;
  }
  return destination.x;
}

static char int_buffer[64];

static char* int_to_string(int n) {
  char* result = &int_buffer[sizeof int_buffer ];
  *--result    = '\0';
  do *--result = n % 10 + '0'; while ((n /= 10) > 0);
  return result;
}
 
int main() {
  SDL_assert(SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1"));
  SDL_assert(SDL_Init(SDL_INIT_VIDEO) == 0);

  SDL_Window* window;
  {
    int x     = SDL_WINDOWPOS_CENTERED;
    int y     = SDL_WINDOWPOS_CENTERED;
    int w     = 500;
    int h     = 500;
    int flags = SDL_WINDOW_ALLOW_HIGHDPI;
    window    = SDL_CreateWindow("Satellites", x, y, w, h, flags);
    SDL_assert(window != NULL);
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, 0, 0);
  SDL_assert(renderer != NULL);

  Image spritesheet = load_image("assets/spritesheet.png");
  SDL_assert(spritesheet.data != NULL);

  SDL_Texture* texture;
  {
    Uint32 format = SDL_PIXELFORMAT_ABGR8888;
    int    access = SDL_TEXTUREACCESS_STATIC;
    int    width  = spritesheet.width;
    int    height = spritesheet.height;
    texture       = SDL_CreateTexture(renderer, format, access, width, height);
    SDL_assert(texture != NULL);
    SDL_assert(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) == 0);
  }

  SDL_assert(SDL_UpdateTexture(texture, NULL, spritesheet.data, spritesheet.pitch()) == 0);

  Uint32 last_ticks     = SDL_GetTicks();
  Uint32 last_guy_ticks = last_ticks;

  satellite_positions [0] = (Vector2) { 0,     0.5 };
  satellite_velocities[0] = (Vector2) { 0.005, 0   };
  satellite_count++;

  guy_count++;

  while (true) {
    int logical_width, logical_height;
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    
    int window_width, window_height;
    SDL_assert(SDL_GetRendererOutputSize(renderer, &window_width, &window_height) == 0);

    float dpi = window_width / logical_width;
    
    SDL_Event event;
    while (SDL_PollEvent(&event) == 1) {
      if (!started && (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)) {
	started = true;
      } else if (event.type == SDL_QUIT) {
	exit(EXIT_SUCCESS);
      } else if (event.type == SDL_KEYDOWN) {
	SDL_KeyboardEvent keyboard_event = event.key;
	SDL_Keysym        keysym         = keyboard_event.keysym;
	SDL_Keycode       keycode        = keysym.sym;
	if (keycode == SDLK_ESCAPE) {
	  exit(EXIT_SUCCESS);
	}
      } else if (event.type == SDL_MOUSEBUTTONDOWN) {
	SDL_MouseButtonEvent button_event = event.button;
	if (button_event.button == SDL_BUTTON_LEFT) {
	  if (deploying == DEPLOYING_NULL) {
	    deploying       = DEPLOYING_START;
	    deploy_start[0] = button_event.x;
	    deploy_start[1] = button_event.y;
	  } else if (deploying == DEPLOYING_END) {
	    if (satellite_count < MAX_SATELLITES) {
	      Vector2 position;
	      position.x = (float) deploy_start[0] / logical_width  * 2 - 1;
	      position.y = 1 - (float) deploy_start[1] / logical_height * 2;

	      Vector2 velocity;
	      velocity.x = (float) (deploy_end  [0] - deploy_start[0]) / logical_width;
	      velocity.y = (float) (deploy_start[1] - deploy_end  [1]) / logical_height;
	      velocity  *= 0.01;
	      
	      satellite_positions[satellite_count]  = position;
	      satellite_velocities[satellite_count] = velocity;
	      satellite_count++;
	    }
	    deploying = DEPLOYING_NULL;
	  }
	}
      } else if (event.type == SDL_MOUSEMOTION) {
	SDL_MouseMotionEvent motion_event = event.motion;
	if (deploying == DEPLOYING_START || deploying == DEPLOYING_END) {
	  deploying     = DEPLOYING_END;
	  deploy_end[0] = motion_event.x;
	  deploy_end[1] = motion_event.y;
	}
      }
    }

    int ticks         = last_ticks;
    int current_ticks = SDL_GetTicks();
    while (current_ticks - ticks >= TICKS_PER_FRAME) {
      planet_rotation += 0.1;

      memset(guy_picked, 0, sizeof guy_picked);
      guy_picked_count = 0;
      
      for (int i = 0; i < satellite_count; i++) {
	Vector2& position = satellite_positions[i];
	Vector2& velocity = satellite_velocities[i];

	position += velocity;

	float gravity = 1 / square_length(position);
	float theta   = atan2f(position.y, position.x);

	velocity.x -= 0.00001 * gravity * cosf(theta);
	velocity.y -= 0.00001 * gravity * sinf(theta);

	bool alive = true;
	for (int j = i + 1; j < satellite_count; j++) {
	  Vector2 other = satellite_positions[j];
	  if (square_length(other - position) < 0.005) {
	    if (explosion_count < MAX_EXPLOSIONS) {
	      explosion_positions[explosion_count] = position;
	      explosion_end_ticks[explosion_count] = ticks + EXPLOSION_TIME;
	      explosion_count++;
	    }
	    delete_satellite(j);
	    j--;
	    alive = false;
	  }
	}

	if (alive) {
	  int   closest_guy      = 0;
	  float closest_distance = INFINITY;
	  for (int j = 0; j < guy_count; j++) {
	    if (guy_picked[j / 64] & (1ul << (j % 64))) {
	      continue;
	    }
	  
	    float guy_angle = guy_angles[j] + to_radians(planet_rotation);
	  
	    Vector2 guy_position;
	    guy_position.x = 0.5 * cosf(guy_angle) * guy_magnitudes[j];
	    guy_position.y = -0.5 * sinf(guy_angle) * guy_magnitudes[j];
	  
	    float distance = square_length(guy_position - position);
	    if (distance < closest_distance && distance < 1) {
	      closest_guy      = j;
	      closest_distance = distance;
	    }	    
	  }

	  if (closest_distance == INFINITY) {
	    satellite_to_guy[i] = -1;
	  } else {
	    satellite_to_guy[i] = closest_guy;
	  
	    guy_picked[closest_guy / 64] |= (1ul << (closest_guy % 64));
	    guy_picked_count++;
	  }
	} else {
	  delete_satellite(i);
	  i--;
	}
      }

      score = max(score, guy_picked_count);

      for (int i = 0; i < explosion_count; i++) {
	int end = explosion_end_ticks[i];
	if (ticks >= end) {
	  delete_explosion(i);
	  i--;
	} else if (end - ticks < EXPLOSION_TIME / 2) {
	  explosion_frame[i] = 1;
	}
      }

      if (ticks >= last_guy_ticks + ticks_per_guy && guy_count < MAX_GUYS) {
	guy_magnitudes[guy_count] = (float) rand() / RAND_MAX;
	guy_angles[guy_count]     = (float) rand() / RAND_MAX * M_PI * 2;
	guy_count++;
	
	last_guy_ticks = ticks;
	if (ticks_per_guy > 0) {
	  ticks_per_guy--;
	}
      }
      
      ticks      += TICKS_PER_FRAME;
      last_ticks  = current_ticks;
    }

    SDL_assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF) == 0);
    SDL_RenderClear(renderer);

    {
      SDL_Rect center = { window_width / 4, window_height / 4, window_width / 2, window_height / 2 };
      int      result = SDL_RenderCopyEx(
	renderer, texture, &planet_sprite, &center, planet_rotation, NULL, SDL_FLIP_NONE
      );
      SDL_assert(result == 0);
    }

    for (int i = 0; i < guy_count; i++) {
      Vector2  position    = get_guy_position(i, window_width, window_height);
      SDL_Rect destination = { position.x, position.y, GUY_WIDTH, GUY_HEIGHT };
      int      result      = SDL_RenderCopyEx(
	renderer, texture, &guy_sprite, &destination, 0, NULL, SDL_FLIP_NONE
      );
      SDL_assert(result == 0);
    }

    for (int i = 0; i < satellite_count; i++) {
      Vector2 position = (satellite_positions[i] + 1) / 2;
      position.y       = 1 - position.y;
      
      position.x *= window_width;
      position.y *= window_height;

      position -= SATELLITE_SIZE / 2;

      if (satellite_to_guy[i] != -1) {
	Vector2 guy_position = get_guy_position(satellite_to_guy[i], window_width, window_height);
	
	int x1 = position.x     + SATELLITE_SIZE / 2;
	int y1 = position.y     + SATELLITE_SIZE / 2;
	int x2 = guy_position.x + GUY_WIDTH      / 2;
	int y2 = guy_position.y + GUY_HEIGHT     / 2;
	SDL_assert(SDL_SetRenderDrawColor(renderer, 0x88, 0x00, 0xFF, 0xFF) == 0);
	SDL_assert(SDL_RenderDrawLine(renderer, x1, y1, x2, y2) == 0);
      }
      
      SDL_Rect destination = { position.x, position.y, SATELLITE_SIZE, SATELLITE_SIZE };
      int      result      = SDL_RenderCopyEx(
	renderer, texture, &satellite_sprite, &destination, 0, NULL, SDL_FLIP_NONE
      );
      SDL_assert(result == 0);
    }

    for (int i = 0; i < explosion_count; i++) {
      SDL_Rect sprite = explosion_sprite;
      sprite.x += explosion_frame[i] * explosion_sprite.w;

      int explosion_size = (float) EXPLOSION_TIME / (explosion_end_ticks[i] - last_ticks) * EXPLOSION_SIZE;
      
      Vector2 position = (explosion_positions[i] + 1) / 2;
      position.y       = 1 - position.y;
      
      position.x *= window_width;
      position.y *= window_height;
      position   -= explosion_size / 2;
      
      SDL_Rect destination = { position.x, position.y, explosion_size, explosion_size };
      int      result      = SDL_RenderCopyEx(
	renderer, texture, &explosion_sprite, &destination, 0, NULL, SDL_FLIP_NONE
      );
      SDL_assert(result == 0);
    }

    if (deploying == DEPLOYING_END) {
      int x1     = deploy_start[0] * dpi;
      int y1     = deploy_start[1] * dpi;
      int x2     = deploy_end[0]   * dpi;
      int y2     = deploy_end[1]   * dpi;
      SDL_assert(SDL_SetRenderDrawColor(renderer, 0xFF, 0, 0, 0xFF) == 0);
      SDL_assert(SDL_RenderDrawLine(renderer, x1, y1, x2, y2) == 0);
    }

    float score_start = draw_text(renderer, texture, 0, 0, "Score ");
    draw_text(renderer, texture, score_start, 0, int_to_string(score));

    if (!started) {
      draw_text(renderer, texture,  32,  90, "Launch satellites by clicking.");
      draw_text(renderer, texture,  32, 154, "Score is max concurrent users.");
      draw_text(renderer, texture, 128, 218, "Press any key to begin.");
    }
    
    SDL_RenderPresent(renderer);
  }
}
