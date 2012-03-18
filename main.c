#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <SDL/SDL.h>
#include <SDL/SDL_gfxPrimitives.h>

#define BIGGIFY(x,y) ((((Uint32)(x))<<16)|(y))
#define NUM_FBs     100
#define NUM_FLs     10
#define SQUARE_DIST 1000
#define LEVEL_LENGTH 700
//#define NO_FIREBALLS
//#define PLAYER_EXPECTED
//#define JUST_SHOW /* no generating of fireballs or anything*/

int WIDTH=640, HEIGHT=480, EXIT_XLOC=LEVEL_LENGTH-30, EXIT_YLOC=200;
int All_ticks = 0;
int PLAYING = 0;
int XDIST=20*20, YDIST=36*36;

SDL_Surface *fb_screen = NULL, *spiral_screen = NULL, *floor_screen = NULL, *door_screen = NULL, *player_screen;

struct FireBall
{
    Sint16 xloc, yloc;
    Sint16 xvel, yvel;
    Sint16 ticksleft, ticks;
};

struct FireBall fireballs[NUM_FBs];

struct Camera
{
    int xloc; /* The left-most corner of the screen */
    int yloc; /* Ditto for vertical stuff */
} camera = {100, 0};

struct FloorSection
{
    Sint16 xloc, yloc; /* where and how high, respectively */
    Sint16 length;
    int view;
    SDL_Surface *surf;
};

struct FloorSection floors[NUM_FLs];

struct Player
{
    Sint16 xloc, yloc;
    Sint16 xvel, yvel;
    Sint16 height, width;
    int rn1, rn2, rn3;
    int path_length;
};

// GENERATING

int is_too_close(Uint32 a, Uint32 b)
{
    Sint16 y1 = a&0xFFFF, x1 = a>>16;
    Sint16 y2 = b&0xFFFF, x2 = b>>16;
    Sint16 d1 = x1-x2, d2 = y1-y2;
    return (d1*d1 < XDIST && d2*d2 < YDIST);
}

int get_jump_height(int start_speed, int time)
{
    int res = 0;
    if(!(time&&start_speed)) return 0;
    do res += start_speed>>1;
    while((start_speed--), (--time));
    return res;
}

Uint32 proper_get_pos(struct Player *player, int ticks)
{
    /*if(ticks < player->rn1) return BIGGIFY(100,100);
    ticks -= player->rn1;
    if(ticks < player->rn2) return BIGGIFY(100+2*ticks, 100);
    ticks -= player->rn2;
    if(ticks < player->rn3) return BIGGIFY(100+2*player->rn2, 100);
    ticks -= player->rn3;
    return BIGGIFY(100+2*player->rn2+2*ticks, 100);*/
    if (ticks < player->rn1) return BIGGIFY(100 + ticks*2, 200);
    ticks -= player->rn1;
    if (ticks < player->rn2) return BIGGIFY(100 + player->rn1*2 + ticks*2, 200-get_jump_height(10, ticks));
    ticks -= player->rn2;
    return BIGGIFY(100 + (player->rn1+player->rn2)*2 + ticks*2, 200-get_jump_height(10,player->rn2));
}

Uint32 player_get_pos(struct Player *player)
{
    return BIGGIFY(player->xloc, player->yloc);
}

Uint32 fireball_get_pos(struct FireBall *fb, int ticks)
{
    Sint16 xloc = fb->xloc, yloc = fb->yloc;
    ticks %= 2*fb->ticks;
    if (ticks > fb->ticks)
    {
        ticks -= fb->ticks;
        xloc += (fb->ticks - ticks)*fb->xvel;
        yloc += (fb->ticks - ticks)*fb->yvel;
    }
    else
    {
        xloc += ticks*fb->xvel;
        yloc += ticks*fb->yvel;
    }
    //fprintf(stderr, "X: %d, Y: %d   ", xloc, yloc);
    return BIGGIFY(xloc, yloc);
}

void add_fireball(struct Player *player, int *n)
{
    Sint16 xloc = rand()%WIDTH, yloc = rand()%HEIGHT;
    Sint16 xvel = (rand()%5)-2, yvel = (rand()%5)-2;
    Sint16 ticks = 50+(rand()%20);
    int i = 0;
    struct FireBall fb = {xloc, yloc, xvel, yvel, ticks, ticks};
    for (i = 0; i < player->path_length; ++ i)
    {
        Uint32 fball = fireball_get_pos(&fb, i), pyer = proper_get_pos(player, i);
        if (is_too_close(fball, pyer))
        {
            -- (*n);
            return;
        }
    }
    fireballs[*n] = fb;
}

void player_set_loc(struct Player *player, Uint32 pos)
{
    player->xloc = (pos>>16);
    player->yloc = (pos&0xFFFF);
}

void generate_floor(struct Player *player)
{
    int i, f;
    struct FloorSection fl;
    for (i = 0, f = 0; i < player->path_length && f < NUM_FLs; ++ i)
    {
        fl.view = 0xCAFEBAB2;
        fl.xloc = (proper_get_pos(player, i)>>16);
        fl.yloc = (proper_get_pos(player, i)&0xFFFF);
        for (; i < player->path_length; ++ i)
        {
            if (fl.yloc != (proper_get_pos(player, i)&0xFFFF))
            {
  //              fprintf(stderr, "Yloc: %d, PGP: %d\n", fl.yloc, proper_get_pos(player, i)&0xFFFF);
                break;
            }
        }
        fl.length = (proper_get_pos(player, i)>>16) - fl.xloc;
        if (fl.length < 7)
            continue;
        fl.yloc += (player->height >> 1)+1;
        int x, y;
        struct FloorSection *F;
        F = &fl;
        SDL_Surface *surf = SDL_CreateRGBSurface(SDL_SWSURFACE, F->length, HEIGHT - F->yloc, 32, 0, 0, 0, 0);
        SDL_Rect rect = {0,0,0,0};
        F->surf = surf;
        for (x = 0; x < F->surf->w; x += floor_screen->w)
        {
            for (y = 0; y < F->surf->h; y += floor_screen->h)
            {
                rect.x = x; rect.y = y;
                SDL_BlitSurface(floor_screen, NULL, F->surf, &rect);
            }
        }
        floors[f] = fl;
        ++ f;
    }
}

// END GENERATING

// PLAYER CONTROLS

int there_is_ground(struct Player *player)
{
    int i;
    struct FloorSection *F = floors;
    for (i = 0; i < NUM_FLs; ++ i, ++ F)
    {
        if (F->view != 0xCAFEBAB2) break;
        if (F->xloc < player->xloc+player->width/2 &&
            F->xloc+F->length > player->xloc-player->width/2 &&
            F->yloc <= player->yloc + player->height/2) return 1;
    }
    return 0;
}

int there_is_wall(struct Player *player)
{
    int i;
    struct FloorSection *F = floors;
    for (i = 0; i < NUM_FLs; ++ i, ++ F)
    {
        if (F->view != 0xCAFEBAB2) break;
        if (F->yloc < player->yloc + player->height/2)
        {
            if (F->xloc < player->xloc + (player->width>>1) &&
                F->xloc+F->length > player->xloc - (player->width>>1)) return 1;
        }
    }
    return 0;
}

void player_draw(struct Player *player, SDL_Surface *screen)
{
#if !defined(JUST_SHOW)
    if (there_is_ground(player)) player->yvel = 0;
    else player->yvel += 1;
#endif
    if (PLAYING)
    {
        int w = there_is_wall(player);
        if (!w) player->xloc += player->xvel;
        player->yloc += player->yvel>>1; // otherwise it drops too fast
    }
    else player_set_loc(player, proper_get_pos(player, All_ticks));
    if (there_is_wall(player)) player->xloc -= player->xvel;
    SDL_Rect rect = {player->xloc-player->width/2-camera.xloc,player->yloc-(player->height)/2-camera.yloc,0,0};
    SDL_BlitSurface(player_screen, NULL, screen, &rect);
    /*boxRGBA(screen, player->xloc - (player->width)/2 - camera.xloc, player->yloc - (player->height)/2 - camera.yloc,
                    player->xloc + (player->width)/2 - camera.xloc, player->yloc + (player->height)/2 - camera.yloc,
                    0, 0, 255, 255);*/
}

int player_update(struct Player *player, SDL_Surface *screen)
{
    int i;
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_QUIT: return -1;
            case SDL_KEYDOWN:
            {
                if(event.key.keysym.sym == SDLK_RIGHT) player->xvel = 2;
                if(event.key.keysym.sym == SDLK_LEFT)  player->xvel = -2;
                if(event.key.keysym.sym == SDLK_UP && there_is_ground(player)) player->yloc -= 5, player->yvel = -9;
                break;
            }
            case SDL_KEYUP:
            {
                if(event.key.keysym.sym == SDLK_RIGHT) player->xvel = 0;
                if(event.key.keysym.sym == SDLK_LEFT)  player->xvel = 0;
                break;
            }
        }
    }
    if (player->xloc < WIDTH/2) camera.xloc = 0;
    else if (player->xloc > LEVEL_LENGTH - WIDTH/2) camera.xloc = LEVEL_LENGTH - WIDTH;
    else camera.xloc = player->xloc - WIDTH/2;
    boxRGBA(screen, 0, 0, WIDTH, HEIGHT, 0, 100, 100, 255);
#if !defined(JUST_SHOW)
    struct FloorSection *F = floors;
    SDL_Rect rect = {0, 0, 0, 0};
    for (i = 0; i < NUM_FLs; ++ i, ++ F)
    {
        if (F->view != 0xCAFEBAB2) break;
        rect.x = F->xloc-camera.xloc; rect.y = F->yloc-camera.yloc;
        SDL_BlitSurface(F->surf, NULL, screen, &rect);
        //boxRGBA(screen, F->xloc, F->yloc, F->xloc + F->length, HEIGHT, 100, 0, 100, 255);
    }
#endif
    rect.x = EXIT_XLOC - camera.xloc - door_screen->w/2;
    rect.y = EXIT_YLOC +1 - camera.yloc - door_screen->h;
    SDL_BlitSurface(door_screen, NULL, screen, &rect);
    player_draw(player, screen);
#if !defined(JUST_SHOW)
//    assert(player_get_pos(player) == proper_get_pos(player, All_ticks));
    Uint32 player_where =
#if !defined(PLAYER_EXPECTED)
                          player_get_pos(player);
#else
                          proper_get_pos(player, All_ticks);
#endif
    if (player->xloc > LEVEL_LENGTH-30) return 2;
    if (player->yloc - player->height/2 > HEIGHT) return 0;
#if !defined(NO_FIREBALLS)
    for (i = 0; i < NUM_FBs; ++ i)
    {
        if(is_too_close(player_where, fireball_get_pos(&(fireballs[i]), All_ticks)))
            return 0;
    }
    for (i = 0; i < NUM_FBs; ++ i)
    {
        Uint32 where = fireball_get_pos(&(fireballs[i]), All_ticks);
        Sint32 x = where>>16;
        Sint32 y = where&0xFFFF;
        SDL_Rect rect = {x-5-camera.xloc, y-5-camera.yloc, 10, 10};
        SDL_BlitSurface(fb_screen, NULL, screen, &rect);
    }
#endif
#endif
    SDL_UpdateRect(screen, 0, 0, 0, 0);
    ++ All_ticks;
//    printf("Ticks: %u\n", All_ticks);
    return 1;
}

// END PLAYER CONTROLS

void win_game(SDL_Surface *screen, SDL_Surface *WS)
{
    int i;
    for (i = 1; i < 50; i += 2)
    {
        boxRGBA(screen, 0, 0, WIDTH, HEIGHT, 255, 255, 255, i);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
        SDL_Delay(50);
    }
    SDL_BlitSurface(WS, NULL, screen, NULL);
    SDL_UpdateRect(screen, 0, 0, 0, 0);
    SDL_Delay(1000);
}

void lose_game(SDL_Surface *screen, SDL_Surface *GOS)
{
    int i;
    for (i = 1; i < 50; i += 2)
    {
        boxRGBA(screen, 0, 0, WIDTH, HEIGHT, 0, 0, 0, i);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
        SDL_Delay(50);
    }
    SDL_BlitSurface(GOS, NULL, screen, NULL);
    SDL_UpdateRect(screen, 0, 0, 0, 0);
    SDL_Delay(1000);
}

void gen_rns(struct Player *player)
{
    srand(time(0));
    player->rn1 = 50 + (rand()%50);
    player->rn2 = 10 + (rand()%30);
    player->rn3 = 50 + (rand()%100);
    player->path_length = WIDTH+player->rn2;
    player->width = player_screen->w;
    player->height = player_screen->h;
    EXIT_YLOC = 200 + player->height/2 - get_jump_height(10, player->rn2);
}

int main()
{
    SDL_Surface *screen, *game_over_screen, *win_screen;
    struct Player player = {100, 200, 0, 0, 30, 25, 0, 0, 0, 0};
    int cont, i;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
        exit(1);
    }

    screen = SDL_SetVideoMode(WIDTH, HEIGHT, 32, SDL_SWSURFACE);
    if (screen == NULL)
    {
        fprintf(stderr, "Unable to set video mode: %s\n", SDL_GetError());
        exit(1);
    }
    game_over_screen = SDL_LoadBMP("imgs/game_over.bmp");
    win_screen = SDL_LoadBMP("imgs/win.bmp");
    fb_screen = SDL_LoadBMP("imgs/fireball.bmp");
    SDL_SetColorKey(fb_screen, SDL_SRCCOLORKEY, SDL_MapRGB(fb_screen->format, 255, 0, 255));
    floor_screen = SDL_LoadBMP("imgs/floor.bmp");
    door_screen = SDL_LoadBMP("imgs/door.bmp");
    SDL_SetColorKey(door_screen, SDL_SRCCOLORKEY, SDL_MapRGB(door_screen->format, 255, 0, 255));
    player_screen = SDL_LoadBMP("imgs/player.bmp");
    SDL_SetColorKey(player_screen, SDL_SRCCOLORKEY, SDL_MapRGB(player_screen->format, 255, 0, 255));

    gen_rns(&player);

#if !defined(JUST_SHOW)
    generate_floor(&player);

    for (i = 0; i < NUM_FBs; ++ i)
    {
        add_fireball(&player, &i);
    }
    //printf("Rejected: %u\n", rejected);
#endif
    do cont = player_update(&player, screen);
    while(SDL_Delay(20), cont == 1);
    if (cont == 2) win_game(screen, win_screen);
    else if (cont == 0) lose_game(screen, game_over_screen);
    exit(0);
}

