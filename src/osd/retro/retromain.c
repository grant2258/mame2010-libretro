/***************************************************************************
retromain.c 
mame2010 - libretro port of mame 0.139
****************************************************************************/

#include <unistd.h>
#include <stdint.h>
#include "osdepend.h"

#include "emu.h"
#include "inpttype.h"
#include "clifront.h"
#include "render.h"
#include "ui.h"
#include "uiinput.h"

#include "libretro.h" 
#include "retromain.h"
#include "file/file_path.h"

#include "rendersw.c"

#include "../../precompile/mameini_boilerplate.h"

#ifdef M16B
	uint16_t videoBuffer[1024*1024];
	#define PITCH 1
#else
	unsigned int videoBuffer[1024*1024];
	#define PITCH 1 * 2
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include "retroogl.c"
#endif

const char* core_name = "mame2010";
char libretro_content_directory[1024];
char libretro_save_directory[1024];
char libretro_system_directory[1024];
char cheatpath[1024];
char samplepath[1024];
char artpath[1024];
char fontpath[1024];
char crosshairpath[1024];
char ctrlrpath[1024];
char inipath[1024];
char cfg_directory[1024];
char nvram_directory[1024];
char memcard_directory[1024];
char input_directory[1024];
char image_directory[1024];
char diff_directory[1024];
char hiscore_directory[1024];
char comment_directory[1024];

int mame_reset = -1;
static int ui_ipt_pushchar=-1;

static bool mouse_enable = true;
static bool videoapproach1_enable = false;
bool hide_nagscreen = false;
bool hide_gameinfo = false;
bool hide_warnings = false;

static void update_geometry();
static bool set_par = false;
static double refresh_rate = 60.0;
static int set_frame_skip;
static unsigned sample_rate = 48000;
unsigned use_external_hiscore = 0;
static int use_auto_mapping =1;
static unsigned adjust_opt[6] = {0/*Enable/Disable*/, 0/*Limit*/, 0/*GetRefreshRate*/, 0/*Brightness*/, 0/*Contrast*/, 0/*Gamma*/};
static float arroffset[3] = {0/*For brightness*/, 0/*For contrast*/, 0/*For gamma*/};

static int rtwi=320,rthe=240,topw=1024; // DEFAULT TEXW/TEXH/PITCH
int SHIFTON = -1;

extern "C" int mmain(int argc, const char *argv);
extern bool draw_this_frame;

retro_video_refresh_t video_cb = NULL;
retro_environment_t environ_cb = NULL;

retro_log_printf_t retro_log = NULL;

static retro_input_state_t input_state_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;

int RLOOP=1;

// rendering target
static render_target *our_target = NULL;

// input device
static input_device *P1_device; // P1 JOYPAD
static input_device *P2_device; // P2 JOYPAD
static input_device *P3_device; // P3 JOYPAD
static input_device *P4_device; // P4 JOYPAD
static input_device *retrokbd_device; // KEYBD
static input_device *mouse_device;    // MOUSE

// state
static UINT32 P1_state[RP_TOTAL];
static UINT32 P2_state[RP_TOTAL];
static UINT32 P3_state[RP_TOTAL];
static UINT32 P4_state[RP_TOTAL];
static UINT16 retrokbd_state[RETROK_LAST];
static UINT16 retrokbd_state2[RETROK_LAST];

int optButtonLayoutP1 = 0; //for player 1
int optButtonLayoutP2 = 0; //for player 2

static int mouseLX,mouseLY;
static int mouseBUT[4];
//static int mouse_enabled;

//enables / disables tate mode
static int tate = 0;
static int screenRot = 0;
int vertical,orient;

static char MgamePath[1024];
static char MgameName[512];

static int FirstTimeUpdate = 1;

bool retro_load_ok  = false;
int pauseg = 0;


/*********************************************
   LOCAL FUNCTION PROTOTYPES
*********************************************/

static void check_variables(void);
static void initInput(running_machine* machine);

/*********************************************/


size_t retro_serialize_size(void){ return 0; }
bool retro_serialize(void *data, size_t size){ return false; }
bool retro_unserialize(const void * data, size_t size){ return false; }

unsigned retro_get_region (void) {return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned type) {return 0;}
size_t retro_get_memory_size(unsigned type) {return 0;}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info){return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2){}
void retro_set_controller_port_device(unsigned in_port, unsigned device){}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { }

void retro_init (void)
{   
    struct retro_log_callback log_cb;

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb))
        retro_log = log_cb.log;
    	
   const char *system_dir  = NULL;
   const char *save_dir    = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
       // use a subfolder in the system directory with the core name (ie mame2010)
        snprintf(libretro_system_directory, sizeof(libretro_system_directory), "%s%s%s", system_dir, path_default_slash(), core_name);
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
       // use a subfolder in the save directory with the core name (ie mame2010)
        snprintf(libretro_save_directory, sizeof(libretro_save_directory), "%s%s%s", save_dir, path_default_slash(), core_name);
   }
   else
   {
        *libretro_save_directory = *libretro_system_directory;
   }
   
    path_mkdir(libretro_system_directory);
    path_mkdir(libretro_save_directory);
 
    // content loaded from mame2010 subfolder within the libretro system folder
    snprintf(samplepath, sizeof(samplepath), "%s%s%s", libretro_system_directory, path_default_slash(), "samples");
    path_mkdir(samplepath);
    snprintf(artpath, sizeof(artpath), "%s%s%s", libretro_system_directory, path_default_slash(), "artwork");
    path_mkdir(artpath);
    snprintf(fontpath, sizeof(fontpath), "%s%s%s", libretro_system_directory, path_default_slash(), "fonts");
    path_mkdir(fontpath);
    snprintf(crosshairpath, sizeof(crosshairpath), "%s%s%s", libretro_system_directory, path_default_slash(), "crosshairs");
    path_mkdir(crosshairpath);

    // user-generated content loaded from mame2010 subfolder within the libretro save folder
    snprintf(ctrlrpath, sizeof(ctrlrpath), "%s%s%s", libretro_save_directory, path_default_slash(), "ctrlr");
    path_mkdir(ctrlrpath);
    snprintf(inipath, sizeof(inipath), "%s%s%s", libretro_save_directory, path_default_slash(), "ini");
    path_mkdir(inipath);
    snprintf(cfg_directory, sizeof(cfg_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "cfg");
    path_mkdir(cfg_directory);
    snprintf(nvram_directory, sizeof(nvram_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "nvram");
    path_mkdir(nvram_directory);
    snprintf(memcard_directory, sizeof(memcard_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "memcard");
    path_mkdir(memcard_directory);
    snprintf(input_directory, sizeof(input_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "input");
    path_mkdir(input_directory);
    snprintf(image_directory, sizeof(image_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "image");
    path_mkdir(image_directory);
    snprintf(diff_directory, sizeof(diff_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "diff");
    path_mkdir(diff_directory);
    snprintf(hiscore_directory, sizeof(hiscore_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "hi");
    path_mkdir(hiscore_directory);    
    snprintf(comment_directory, sizeof(comment_directory), "%s%s%s", libretro_save_directory, path_default_slash(), "comment");
    path_mkdir(comment_directory);

    char mameini_path[1024];
    
    snprintf(mameini_path, sizeof(mameini_path), "%s%s%s", inipath, path_default_slash(), "mame.ini");
    if(!path_is_valid(mameini_path))
    {
        retro_log(RETRO_LOG_INFO, "[MAME 2010] mame.ini not found at: %s\n", mameini_path);
        
        FILE *mameini_file;
        if((mameini_file=fopen(mameini_path, "wb"))==NULL)
        {
            retro_log(RETRO_LOG_ERROR, "[MAME 2010] something went wrong generating new mame.ini at: %s\n", mameini_path);
        }
        else
        {
            fwrite(mameini_boilerplate, sizeof(char), mameini_boilerplate_length, mameini_file);          
            fclose(mameini_file);
            retro_log(RETRO_LOG_INFO, "[MAME 2010] new mame.ini generated at: %s\n", mameini_path);            
        }
    }
    else
        retro_log(RETRO_LOG_INFO, "[MAME 2010] mame.ini found at: %s\n", mameini_path);

}

bool retro_load_game(const struct retro_game_info *info)
{
   strncpy(libretro_content_directory, info->path, sizeof(libretro_content_directory)-1 );
   path_basedir(libretro_content_directory);
   
   retro_log(RETRO_LOG_INFO, "[MAME 2010] libretro_content_directory: %s\n", libretro_content_directory);  
   retro_log(RETRO_LOG_INFO, "[MAME 2010] libretro_system_directory: %s\n", libretro_system_directory);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] libretro_save directory: %s\n", libretro_save_directory); 
   
#if 0
   struct retro_keyboard_callback cb = { keyboard_cb };
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cb);
#endif

#ifdef M16B
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
#else
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
#endif

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      retro_log(RETRO_LOG_ERROR, "[MAME 2010] RGB pixel format is not supported.\n");
      exit(0);
   }
   check_variables();

#ifdef M16B
   memset(videoBuffer, 0, 1024*1024*2);
#else
   memset(videoBuffer, 0, 1024*1024*2*2);
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#ifdef HAVE_OPENGLES
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;
#endif

   init_input_descriptors();
   
   if(mmain(1,info->path)!=1){ // path the romset path to the mmain function to start emulation
        retro_log(RETRO_LOG_ERROR, "[MAME 2010] MAME returned an error!\n");
		return 0;
   } 

   retro_load_ok  = true;
   video_set_frameskip(set_frame_skip);

   for (int i = 0; i < 6; i++)
	adjust_opt[i] = 1;

   return 1;
}

void osd_exit(running_machine &machine)
{
   retro_log(RETRO_LOG_INFO, "[MAME 2010] osd_exit called \n");

   if (our_target != NULL)
      render_target_free(our_target);
   our_target = NULL;

   global_free(P1_device);
   global_free(P2_device);
   global_free(retrokbd_device);
   global_free(mouse_device);
}

void osd_init(running_machine* machine)
{
   retro_log(RETRO_LOG_INFO, "[MAME 2010] osd_init starting\n");
  
   int gamRot=0;

   machine->add_notifier(MACHINE_NOTIFY_EXIT, osd_exit);

   our_target = render_target_alloc(machine,NULL, 0);

   initInput(machine);

   retro_log(RETRO_LOG_INFO, "[MAME 2010] Machine screen orientation: %s \n",
         (machine->gamedrv->flags & ORIENTATION_SWAP_XY) ? "VERTICAL" : "HORIZONTAL"
         );

   orient = (machine->gamedrv->flags & ORIENTATION_MASK);
   vertical = (machine->gamedrv->flags & ORIENTATION_SWAP_XY);

   gamRot = (ROT270 == orient) ? 1 : gamRot;
   gamRot = (ROT180 == orient) ? 2 : gamRot;
   gamRot = (ROT90 == orient) ? 3 : gamRot;

   prep_retro_rotation(gamRot);
   machine->sample_rate = sample_rate;	/* Override original value */

   retro_log(RETRO_LOG_INFO, "[MAME 2010] osd_init done\n");
}

bool draw_this_frame;

void osd_update(running_machine *machine,int skip_redraw)
{
   const render_primitive_list *primlist;
   UINT8 *surfptr;

   if (mame_reset == 1)
   {
      machine->schedule_soft_reset();
      mame_reset = -1;
   }

   if(pauseg==-1){
      machine->schedule_exit();
      return;
   }

   if (FirstTimeUpdate == 1)
      skip_redraw = 0; //force redraw to make sure the video texture is created

   if (!skip_redraw)
   {

      draw_this_frame = true;
      // get the minimum width/height for the current layout
      int minwidth, minheight;

      if(videoapproach1_enable==false){
         render_target_get_minimum_size(our_target,&minwidth, &minheight);
      }
      else{
         minwidth=1024;minheight=768;
      }

      if (adjust_opt[0])
      {
		adjust_opt[0] = 0;

		if (adjust_opt[2])
		{
			adjust_opt[2] = 0;
			refresh_rate = (machine->primary_screen == NULL) ? screen_device::k_default_frame_rate : ATTOSECONDS_TO_HZ(machine->primary_screen->frame_period().attoseconds);
			update_geometry();
		}

		if ((adjust_opt[3] || adjust_opt[4] || adjust_opt[5]) && adjust_opt[1])
		{
			screen_device *screen = screen_first(*machine);
			render_container *container = render_container_get_screen(screen);
			render_container_user_settings settings;
			render_container_get_user_settings(container, &settings);

			if (adjust_opt[3])
			{
				adjust_opt[3] = 0;
				settings.brightness = arroffset[0] + 1.0f;
				render_container_set_user_settings(container, &settings);
			}
			if (adjust_opt[4])
			{
				adjust_opt[4] = 0;
				settings.contrast = arroffset[1] + 1.0f;
				render_container_set_user_settings(container, &settings);
			}
			if (adjust_opt[5])
			{
				adjust_opt[5] = 0;
				settings.gamma = arroffset[2] + 1.0f;
				render_container_set_user_settings(container, &settings);
			}
		}
      }

      if (FirstTimeUpdate == 1) {

         FirstTimeUpdate++;
         retro_log(RETRO_LOG_INFO, "[MAME 2010] game screen w=%i h=%i  rowPixels=%i\n", minwidth, minheight,minwidth );

         rtwi=minwidth;
         rthe=minheight;
         topw=minwidth;

         int gamRot=0;
         orient  = (machine->gamedrv->flags & ORIENTATION_MASK);
         vertical = (machine->gamedrv->flags & ORIENTATION_SWAP_XY);

         gamRot = (ROT270 == orient) ? 1 : gamRot;
         gamRot = (ROT180 == orient) ? 2 : gamRot;
         gamRot = (ROT90  == orient) ? 3 : gamRot;

         prep_retro_rotation(gamRot);
      }

      if (minwidth != rtwi || minheight != rthe ){
         retro_log(RETRO_LOG_INFO, "[MAME 2010] Res change: old(%d,%d) new(%d,%d) %d\n",rtwi,rthe,minwidth,minheight,topw);
         rtwi=minwidth;
         rthe=minheight;
         topw=minwidth;

	 adjust_opt[0] = adjust_opt[2] = 1;
      }
/*    No need
      if(videoapproach1_enable){
         rtwi=topw=1024;
         rthe=768;
      } */

      // make that the size of our target
      render_target_set_bounds(our_target,rtwi,rthe, 0);
      // our_target->set_bounds(rtwi,rthe);
      // get the list of primitives for the target at the current size
      // render_primitive_list &primlist = our_target->get_primitives();
      primlist = render_target_get_primitives(our_target);
      // lock them, and then render them
      //      primlist.acquire_lock();
      osd_lock_acquire(primlist->lock);

      surfptr = (UINT8 *) videoBuffer;
#ifdef M16B
      rgb565_draw_primitives(primlist->head, surfptr,rtwi,rthe,rtwi);
#else
      rgb888_draw_primitives(primlist->head, surfptr, rtwi,rthe,rtwi);
#endif
#if 0
      surfptr = (UINT8 *) videoBuffer;

      //  draw a series of primitives using a software rasterizer
      for (const render_primitive *prim = primlist.first(); prim != NULL; prim = prim->next())
      {
         switch (prim->type)
         {
            case render_primitive::LINE:
               draw_line(*prim, (PIXEL_TYPE*)surfptr, minwidth, minheight, minwidth);
               break;

            case render_primitive::QUAD:
               if (!prim->texture.base)
                  draw_rect(*prim, (PIXEL_TYPE*)surfptr, minwidth, minheight, minwidth);
               else
                  setup_and_draw_textured_quad(*prim, (PIXEL_TYPE*)surfptr, minwidth, minheight, minwidth);
               break;

            default:
               throw emu_fatalerror("Unexpected render_primitive type");
         }
      }
#endif
      osd_lock_release(primlist->lock);


      //  primlist.release_lock();
   } 
   else
      draw_this_frame = false;

   RLOOP=0;

   if(ui_ipt_pushchar!=-1)
   {
      ui_input_push_char_event(machine, our_target, (unicode_char)ui_ipt_pushchar);
      ui_ipt_pushchar=-1;
   }
}

void osd_wait_for_debugger(running_device *device, int firststop)
{
   // we don't have a debugger, so we just return here
}

void osd_update_audio_stream(running_machine *machine,short *buffer, int samples_this_frame) 
{
	if(pauseg!=-1)audio_batch_cb(buffer, samples_this_frame);
}

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "mame2010_use_auto_mapping", "AutoMapping enabled(restart needed) ; disabled|enabled" },
      { "mame2010_mouse_enable", "Mouse enabled; enabled|disabled" },
      { "mame2010_videoapproach1_enable", "Video approach 1 Enabled; disabled|enabled" },
      { "mame2010_skip_nagscreen", "Hide nag screen; enabled|disabled" },
      { "mame2010_skip_gameinfo", "Hide game info screen; disabled|enabled" },
      { "mame2010_skip_warnings", "Hide warning screen; disabled|enabled" },
      { "mame2010_aspect_ratio", "Core provided aspect ratio; DAR|PAR" },
      { "mame2010_frame_skip", "Set frameskip; 0|1|2|3|4|automatic" },
      { "mame2010_sample_rate", "Set sample rate (Restart); 48000Hz|44100Hz|32000Hz|22050Hz" },
      { "mame2010_adj_brightness",
	"Set brightness; default|+1%|+2%|+3%|+4%|+5%|+6%|+7%|+8%|+9%|+10%|+11%|+12%|+13%|+14%|+15%|+16%|+17%|+18%|+19%|+20%|-20%|-19%|-18%|-17%|-16%|-15%|-14%|-13%|-12%|-11%|-10%|-9%|-8%|-7%|-6%|-5%|-4%|-3%|-2%|-1%" },
      { "mame2010_adj_contrast",
	"Set contrast; default|+1%|+2%|+3%|+4%|+5%|+6%|+7%|+8%|+9%|+10%|+11%|+12%|+13%|+14%|+15%|+16%|+17%|+18%|+19%|+20%|-20%|-19%|-18%|-17%|-16%|-15%|-14%|-13%|-12%|-11%|-10%|-9%|-8%|-7%|-6%|-5%|-4%|-3%|-2%|-1%" },
      { "mame2010_adj_gamma",
	"Set gamma; default|+1%|+2%|+3%|+4%|+5%|+6%|+7%|+8%|+9%|+10%|+11%|+12%|+13%|+14%|+15%|+16%|+17%|+18%|+19%|+20%|-20%|-19%|-18%|-17%|-16%|-15%|-14%|-13%|-12%|-11%|-10%|-9%|-8%|-7%|-6%|-5%|-4%|-3%|-2%|-1%" },
      { "mame2010_external_hiscore", "Use external hiscore.dat; disabled|enabled" },
      { NULL, NULL },
   };

   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

static void check_variables(void)
{
   struct retro_variable var = {0};
   bool tmp_ar = set_par;

   var.key = "mame2010_mouse_enable";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      retro_log(RETRO_LOG_INFO, "[MAME 2010] mouse_enable value: %s\n", var.value);
      if (!strcmp(var.value, "disabled"))
         mouse_enable = false;
      if (!strcmp(var.value, "enabled"))
         mouse_enable = true;
   }

   var.key = "mame2010_skip_nagscreen";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      retro_log(RETRO_LOG_INFO, "[MAME 2010] skip_nagscreen value: %s\n", var.value);
      if (!strcmp(var.value, "disabled"))
         hide_nagscreen = false;
      if (!strcmp(var.value, "enabled"))
         hide_nagscreen = true;
   }

   var.key = "mame2010_skip_gameinfo";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      retro_log(RETRO_LOG_INFO, "[MAME 2010] skip_gameinfo value: %s\n", var.value);
      if (!strcmp(var.value, "disabled"))
         hide_gameinfo = false;
      if (!strcmp(var.value, "enabled"))
         hide_gameinfo = true;
   }

   var.key = "mame2010_skip_warnings";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      retro_log(RETRO_LOG_INFO, "[MAME 2010] skip_warnings value: %s\n", var.value);
      if (!strcmp(var.value, "disabled"))
         hide_warnings = false;
      if (!strcmp(var.value, "enabled"))
         hide_warnings = true;
   }

   var.key = "mame2010_videoapproach1_enable";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      retro_log(RETRO_LOG_INFO, "[MAME 2010] videoapproach1_enable value: %s\n", var.value);
      if (!strcmp(var.value, "disabled"))
         videoapproach1_enable = false;
      if (!strcmp(var.value, "enabled"))
         videoapproach1_enable = true;
   }

   var.key = "mame2010_frame_skip";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	int temp_fs = set_frame_skip;
	if (!strcmp(var.value, "automatic"))
		set_frame_skip = -1;
	else
		set_frame_skip = atoi(var.value);

	if (temp_fs != set_frame_skip)
		video_set_frameskip(set_frame_skip);
   }

   var.key = "mame2010_sample_rate";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	sample_rate = atoi(var.value);

   var.key = "mame2010_aspect_ratio";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      	if (!strcmp(var.value, "PAR"))
		set_par = true;
	else
		set_par = false;
   }

   var.key = "mame2010_adj_brightness";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	float temp_value = arroffset[0];
	if (!strcmp(var.value, "default"))
		arroffset[0] = 0.0;
	else
		arroffset[0] = (float)atoi(var.value) / 100.0f;

	if (temp_value != arroffset[0])
		adjust_opt[0] = adjust_opt[3] = 1;
   }

   var.key = "mame2010_adj_contrast";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	float temp_value = arroffset[1];
	if (!strcmp(var.value, "default"))
		arroffset[1] = 0.0;
	else
		arroffset[1] = (float)atoi(var.value) / 100.0f;

	if (temp_value != arroffset[1])
		adjust_opt[0] = adjust_opt[4] = 1;
   }

   var.key = "mame2010_adj_gamma";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	float temp_value = arroffset[2];
	if (!strcmp(var.value, "default"))
		arroffset[2] = 0.0;
	else
		arroffset[2] = (float)atoi(var.value) / 100.0f;
  
	if (temp_value != arroffset[2])
		adjust_opt[0] = adjust_opt[5] = 1;
   }

   if (tmp_ar != set_par)
	update_geometry();

   var.value = NULL;
   var.key = "mame2010_external_hiscore";
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if(strcmp(var.value, "enabled") == 0)
         use_external_hiscore = 1;
      else
         use_external_hiscore = 0;    
    }

   var.key = "mame2010_use_auto_mapping";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         use_auto_mapping = false;
      if (!strcmp(var.value, "enabled"))
         use_auto_mapping = true;
   }
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

static void update_geometry()
{
   struct retro_system_av_info av_info;
   retro_get_system_av_info( &av_info);
   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "MAME 2010";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version = "0.139" GIT_VERSION;
   info->valid_extensions = "zip|chd|7z";
   info->need_fullpath = true;
   info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width   = rtwi;
   info->geometry.base_height  = rthe;

   info->geometry.max_width    = 1024;
   info->geometry.max_height   = 768;

   float display_ratio 	= set_par ? (vertical ? (float)rthe / (float)rtwi : (float)rtwi / (float)rthe) : (vertical ? 3.0f / 4.0f : 4.0f / 3.0f);
   info->geometry.aspect_ratio = display_ratio;

   info->timing.fps            = refresh_rate;
   info->timing.sample_rate    = (double)sample_rate;

#if 0	/* Test */
	int common_factor = 1;
	if (set_par)
	{
		int temp_width = rtwi;
		int temp_height = rthe;
		while (temp_width != temp_height)
		{
			if (temp_width > temp_height)
				temp_width -= temp_height;
			else
				temp_height -= temp_width;
		}
		common_factor = temp_height;
	}
	retro_log(RETRO_LOG_INFO, "Current aspect ratio = %d : %d , screen refresh rate = %f , sound sample rate = %.1f \n", set_par ? vertical ? rthe / common_factor : rtwi / common_factor :
			vertical ? 3 : 4, set_par ? vertical ? rtwi / common_factor : rthe / common_factor : vertical ? 4 : 3, info->timing.fps, info->timing.sample_rate);
#endif
}

void retro_deinit(void)
{
   if(retro_load_ok)retro_finish();
   retro_log(RETRO_LOG_INFO, "[MAME 2010] retro_deinit called\n");
}

void retro_reset (void)
{
   mame_reset = 1;
}

void retro_run (void)
{
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

	retro_poll_mame_input();
	retro_main_loop();

	RLOOP = 1;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
	do_gl2d();
#else
	if (draw_this_frame)
      		video_cb(videoBuffer,rtwi, rthe, topw << PITCH);
   	else
      		video_cb(NULL,rtwi, rthe, topw << PITCH);
#endif

}

void prep_retro_rotation(int rot)
{
   retro_log(RETRO_LOG_INFO, "[MAME 2010] Rotation:%d\n",rot);
   environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &rot);
}

void retro_unload_game(void)
{
	if(pauseg == 0)
		pauseg = -1;

	retro_log(RETRO_LOG_INFO, "[MAME 2010] Retro unload_game\n");
}

void init_input_descriptors(void)
{
   #define describe_buttons(INDEX) \
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,     "Joystick Left" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,    "Joystick Right" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Joystick Up" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,     "Joystick Down" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,        "Button 1" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,        "Button 2" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,        "Button 3" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,        "Button 4" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,        "Button 5" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,        "Button 6" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,       "Button 7" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,       "Button 8" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,       "Button 9" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,       "Button 10"},\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Insert Coin" },\
   { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },\
//todo add analog descriptions

   struct retro_input_descriptor desc[] = {

      /* start with gui function keys unique to the Player 1 joypad */
      describe_buttons(0)
      describe_buttons(1)
      describe_buttons(2)
      describe_buttons(3)
      { 0, 0, 0, 0, NULL }
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

#define input_device_item_add_mouse(a,b,c,d,e) input_device_item_add(a,b,c,d,e)
#define input_device_item_add_kbd(a,b,c,d,e) input_device_item_add(a,b,c,d,e)


static INT32 retrokbd_get_state(void *device_internal, void *item_internal)
{
   UINT8 *itemdata = (UINT8 *)item_internal;
   return *itemdata;
}

static INT32 generic_axis_get_state(void *device_internal, void *item_internal)
{
   INT32 *axisdata = (INT32 *)item_internal;
   return *axisdata;
}

static INT32 generic_button_get_state(void *device_internal, void *item_internal)
{
   INT32 *itemdata = (INT32 *)item_internal;
   return *itemdata;
}

static void initInput(running_machine* machine)
{
   int i,button;
   char defname[20];

   if (mouse_enable)
   {

      mouse_device = input_device_add(machine, DEVICE_CLASS_MOUSE, "Mice1", NULL);
      // add the axes
      input_device_item_add_mouse(mouse_device, "X", &mouseLX, ITEM_ID_XAXIS, generic_axis_get_state);
      input_device_item_add_mouse(mouse_device, "Y", &mouseLY, ITEM_ID_YAXIS, generic_axis_get_state);

      for (button = 0; button < 4; button++)
      {
         input_item_id itemid = (input_item_id) (ITEM_ID_BUTTON1 + button);
         snprintf(defname, sizeof(defname), "B%d", button + 1);

         input_device_item_add_mouse(mouse_device, defname, &mouseBUT[button], itemid, generic_button_get_state);
      }
   }

   
   // our faux keyboard only has a couple of keys (corresponding to the common defaults
   P1_device = input_device_add(machine, DEVICE_CLASS_JOYSTICK, "Retropad1", NULL);
   P2_device = input_device_add(machine, DEVICE_CLASS_JOYSTICK, "Retropad2", NULL);
   P3_device = input_device_add(machine, DEVICE_CLASS_JOYSTICK, "Retropad3", NULL);
   P4_device = input_device_add(machine, DEVICE_CLASS_JOYSTICK, "Retropad4", NULL);

   if (P1_device == NULL)
      fatalerror("P1 Error creating retropad device\n");

   if (P2_device == NULL)
      fatalerror("P2 Error creating retropad device\n");

   if (P3_device == NULL)
      fatalerror("P3 Error creating retropad device\n");

   if (P4_device == NULL)
      fatalerror("P4 Error creating retropad device\n");
 
   retro_log(RETRO_LOG_INFO, "[MAME 2010] SOURCE FILE: %s\n", machine->gamedrv->source_file);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] PARENT: %s\n", machine->gamedrv->parent);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] NAME: %s\n", machine->gamedrv->name);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] DESCRIPTION: %s\n", machine->gamedrv->description);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] YEAR: %s\n", machine->gamedrv->year);
   retro_log(RETRO_LOG_INFO, "[MAME 2010] MANUFACTURER: %s\n", machine->gamedrv->manufacturer);
 

 
   input_device_item_add(P1_device, "axis 0",     &P1_state[RP_LX],    ITEM_ID_XAXIS,         generic_axis_get_state);
   input_device_item_add(P1_device, "axis 1",     &P1_state[RP_LY],    ITEM_ID_YAXIS,         generic_axis_get_state);
   input_device_item_add(P1_device, "axis 2",     &P1_state[RP_RX],    ITEM_ID_RXAXIS,        generic_axis_get_state);
   input_device_item_add(P1_device, "axis 3",     &P1_state[RP_RY],    ITEM_ID_RYAXIS,        generic_axis_get_state);
   input_device_item_add(P1_device, "Start",      &P1_state[RP_ST],    ITEM_ID_START,         generic_button_get_state);
   input_device_item_add(P1_device, "Sel",        &P1_state[RP_SL],    ITEM_ID_SELECT,        generic_button_get_state);
   input_device_item_add(P1_device, "DPAD UP",    &P1_state[RP_UP],    ITEM_ID_HAT1UP,        generic_button_get_state);
   input_device_item_add(P1_device, "DPAD DOWN",  &P1_state[RP_DOWN],  ITEM_ID_HAT1DOWN,      generic_button_get_state);
   input_device_item_add(P1_device, "DPAD Left",  &P1_state[RP_LEFT],  ITEM_ID_HAT1LEFT,      generic_button_get_state);
   input_device_item_add(P1_device, "DPAD Right", &P1_state[RP_RIGHT], ITEM_ID_HAT1RIGHT,     generic_button_get_state);

   input_device_item_add(P2_device, "axis 0",     &P2_state[RP_LX],    ITEM_ID_XAXIS,         generic_axis_get_state);
   input_device_item_add(P2_device, "axis 1",     &P2_state[RP_LY],    ITEM_ID_YAXIS,         generic_axis_get_state);
   input_device_item_add(P2_device, "axis 2",     &P2_state[RP_RX],    ITEM_ID_RXAXIS,        generic_axis_get_state);
   input_device_item_add(P2_device, "axis 3",     &P2_state[RP_RY],    ITEM_ID_RYAXIS,        generic_axis_get_state);
   input_device_item_add(P2_device, "Start",      &P2_state[RP_ST],    ITEM_ID_START,         generic_button_get_state);
   input_device_item_add(P2_device, "Sel",        &P2_state[RP_SL],    ITEM_ID_SELECT,        generic_button_get_state);
   input_device_item_add(P2_device, "DPAD UP",    &P2_state[RP_UP],    ITEM_ID_HAT1UP,        generic_button_get_state);
   input_device_item_add(P2_device, "DPAD Down" , &P2_state[RP_DOWN],  ITEM_ID_HAT1DOWN,      generic_button_get_state);
   input_device_item_add(P2_device, "DPAD Left",  &P2_state[RP_LEFT],  ITEM_ID_HAT1LEFT,      generic_button_get_state);
   input_device_item_add(P2_device, "DPAD Right", &P2_state[RP_RIGHT], ITEM_ID_HAT1RIGHT,     generic_button_get_state);

   input_device_item_add(P3_device, "axis 0",     &P3_state[RP_LX],    ITEM_ID_XAXIS,         generic_axis_get_state);
   input_device_item_add(P3_device, "axis 1",     &P3_state[RP_LY],    ITEM_ID_YAXIS,         generic_axis_get_state);
   input_device_item_add(P3_device, "axis 2",     &P3_state[RP_RX],    ITEM_ID_RXAXIS,        generic_axis_get_state);
   input_device_item_add(P3_device, "axis 3",     &P3_state[RP_RY],    ITEM_ID_RYAXIS,        generic_axis_get_state);
   input_device_item_add(P3_device, "Start",      &P3_state[RP_ST],    ITEM_ID_START,         generic_button_get_state);
   input_device_item_add(P3_device, "Sel",        &P3_state[RP_SL],    ITEM_ID_SELECT,        generic_button_get_state);
   input_device_item_add(P3_device, "DPAD UP",    &P3_state[RP_UP],    ITEM_ID_HAT1UP,        generic_button_get_state);
   input_device_item_add(P3_device, "DPAD Down",  &P3_state[RP_DOWN],  ITEM_ID_HAT1DOWN,      generic_button_get_state);
   input_device_item_add(P3_device, "DPAD Left",  &P3_state[RP_LEFT],  ITEM_ID_HAT1LEFT,      generic_button_get_state);
   input_device_item_add(P3_device, "DPAD Right", &P3_state[RP_RIGHT], ITEM_ID_HAT1RIGHT,     generic_button_get_state);

   input_device_item_add(P4_device, "axis 0",     &P4_state[RP_LX],    ITEM_ID_XAXIS,         generic_axis_get_state);
   input_device_item_add(P4_device, "axis 1",     &P4_state[RP_LY],    ITEM_ID_YAXIS,         generic_axis_get_state);
   input_device_item_add(P4_device, "axis 2",     &P4_state[RP_RX],    ITEM_ID_RXAXIS,        generic_axis_get_state);
   input_device_item_add(P4_device, "axis 3",     &P4_state[RP_RY],    ITEM_ID_RYAXIS,        generic_axis_get_state);
   input_device_item_add(P4_device, "Start",      &P4_state[RP_ST],    ITEM_ID_START,         generic_button_get_state);
   input_device_item_add(P4_device, "Sel",        &P4_state[RP_SL],    ITEM_ID_SELECT,        generic_button_get_state);
   input_device_item_add(P4_device, "DPAD UP",    &P4_state[RP_UP],    ITEM_ID_HAT1UP,        generic_button_get_state);
   input_device_item_add(P4_device, "DPAD Down",  &P4_state[RP_DOWN],  ITEM_ID_HAT1DOWN,      generic_button_get_state);
   input_device_item_add(P4_device, "DPAD Left",  &P4_state[RP_LEFT],  ITEM_ID_HAT1LEFT,      generic_button_get_state);
   input_device_item_add(P4_device, "DPAD Right", &P4_state[RP_RIGHT], ITEM_ID_HAT1RIGHT,     generic_button_get_state);


   if ( ( TEKKEN_LAYOUT ) && (use_auto_mapping) )	/* Tekken 1/2 */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON4, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON4, generic_button_get_state);
   }
   else if ( ( SOULEDGE_LAYOUT ) && (use_auto_mapping) )    /* Soul Edge / Soul Calibur */   
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
   }
   else if ( ( DOA_LAYOUT ) && (use_auto_mapping) )      /* Dead or Alive++ */
   {
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);

      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
   }
   else if ( ( VF_LAYOUT ) && (use_auto_mapping) )      /* Virtua Fighter */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON3, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON3, generic_button_get_state);
   }
   else if ( ( EHRGEIZ_LAYOUT ) && (use_auto_mapping) )     /* Ehrgeiz */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON3, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON3, generic_button_get_state);
   }
   else if ( ( TS2_LAYOUT ) && (use_auto_mapping) )     /* Toshinden 2 */
   {
      input_device_item_add(P1_device, "L", &P1_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_Y], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON6, generic_button_get_state);

      input_device_item_add(P2_device, "L", &P2_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON6, generic_button_get_state);
   }
   else if ( ( SF_LAYOUT  ) && (use_auto_mapping) )     /* Capcom 6-button fighting games */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "L", &P1_state[RP_L], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "L", &P2_state[RP_L], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);
   }
 
   else if ( ( KINST_LAYOUT ) && (use_auto_mapping) )     /* Killer Instinct 1 */
   {
      input_device_item_add(P1_device, "L", &P1_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON6, generic_button_get_state);

      input_device_item_add(P2_device, "L", &P2_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON6, generic_button_get_state);
   }
   else if ( ( KINST2_LAYOUT  ) && (use_auto_mapping) )     /* Killer Instinct 2 */
   {
      input_device_item_add(P1_device, "L", &P1_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);

      input_device_item_add(P2_device, "L", &P2_state[RP_L], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);
   }
   else if ( ( TEKKEN3_LAYOUT  ) && (use_auto_mapping) )     /* Tekken 3 / Tekken Tag Tournament */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
   }
   else if ( ( MK_LAYOUT ) &&  (use_auto_mapping) )     /* Mortal Kombat 1/2/3 / Ultimate/WWF: Wrestlemania */
   {
      input_device_item_add(P1_device, "Y", &P1_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P1_device, "L", &P1_state[RP_L], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P1_device, "X", &P1_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P1_device, "B", &P1_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P1_device, "A", &P1_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P1_device, "R", &P1_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);

      input_device_item_add(P2_device, "Y", &P2_state[RP_Y], ITEM_ID_BUTTON1, generic_button_get_state);
      input_device_item_add(P2_device, "L", &P2_state[RP_L], ITEM_ID_BUTTON2, generic_button_get_state);
      input_device_item_add(P2_device, "X", &P2_state[RP_X], ITEM_ID_BUTTON3, generic_button_get_state);
      input_device_item_add(P2_device, "B", &P2_state[RP_B], ITEM_ID_BUTTON4, generic_button_get_state);
      input_device_item_add(P2_device, "A", &P2_state[RP_A], ITEM_ID_BUTTON5, generic_button_get_state);
      input_device_item_add(P2_device, "R", &P2_state[RP_R], ITEM_ID_BUTTON6, generic_button_get_state);
   }
   else
   {
      input_device_item_add(P1_device, "B",      &P1_state[RP_B],     ITEM_ID_BUTTON1 ,      generic_button_get_state);
      input_device_item_add(P1_device, "A",      &P1_state[RP_A],     ITEM_ID_BUTTON2 ,      generic_button_get_state);
      input_device_item_add(P1_device, "Y",      &P1_state[RP_Y],     ITEM_ID_BUTTON3,       generic_button_get_state);
      input_device_item_add(P1_device, "X",      &P1_state[RP_X],     ITEM_ID_BUTTON4,       generic_button_get_state);
      input_device_item_add(P1_device, "R",      &P1_state[RP_R],     ITEM_ID_BUTTON5,       generic_button_get_state);
      input_device_item_add(P1_device, "L",      &P1_state[RP_L],     ITEM_ID_BUTTON6,       generic_button_get_state);
      input_device_item_add(P1_device, "R2",     &P1_state[RP_R2],    ITEM_ID_BUTTON7,       generic_button_get_state);
      input_device_item_add(P1_device, "L2",     &P1_state[RP_L2],    ITEM_ID_BUTTON8,       generic_button_get_state);
      input_device_item_add(P1_device, "R3",     &P1_state[RP_R3],    ITEM_ID_BUTTON9,       generic_button_get_state);
      input_device_item_add(P1_device, "L3",     &P1_state[RP_L3],    ITEM_ID_BUTTON10,      generic_button_get_state);

      input_device_item_add(P2_device, "B",      &P2_state[RP_B],     ITEM_ID_BUTTON1 ,      generic_button_get_state);
      input_device_item_add(P2_device, "A",      &P2_state[RP_A],     ITEM_ID_BUTTON2 ,      generic_button_get_state);
      input_device_item_add(P2_device, "Y",      &P2_state[RP_Y],     ITEM_ID_BUTTON3,       generic_button_get_state);
      input_device_item_add(P2_device, "X",      &P2_state[RP_X],     ITEM_ID_BUTTON4,       generic_button_get_state);
      input_device_item_add(P2_device, "R",      &P2_state[RP_R],     ITEM_ID_BUTTON5,       generic_button_get_state);
      input_device_item_add(P2_device, "L",      &P2_state[RP_L],     ITEM_ID_BUTTON6,       generic_button_get_state);
      input_device_item_add(P2_device, "R2",     &P2_state[RP_R2],    ITEM_ID_BUTTON7,       generic_button_get_state);
      input_device_item_add(P2_device, "L2",     &P2_state[RP_L2],    ITEM_ID_BUTTON8,       generic_button_get_state);
      input_device_item_add(P2_device, "R3",     &P2_state[RP_R3],    ITEM_ID_BUTTON9,       generic_button_get_state);
      input_device_item_add(P2_device, "L3",     &P2_state[RP_L3],    ITEM_ID_BUTTON10,      generic_button_get_state);

      input_device_item_add(P3_device, "B",      &P3_state[RP_B],     ITEM_ID_BUTTON1 ,      generic_button_get_state);
      input_device_item_add(P3_device, "A",      &P3_state[RP_A],     ITEM_ID_BUTTON2 ,      generic_button_get_state);
      input_device_item_add(P3_device, "Y",      &P3_state[RP_Y],     ITEM_ID_BUTTON3,       generic_button_get_state);
      input_device_item_add(P3_device, "X",      &P3_state[RP_X],     ITEM_ID_BUTTON4,       generic_button_get_state);
      input_device_item_add(P3_device, "R",      &P3_state[RP_R],     ITEM_ID_BUTTON5,       generic_button_get_state);
      input_device_item_add(P3_device, "L",      &P3_state[RP_L],     ITEM_ID_BUTTON6,       generic_button_get_state);
      input_device_item_add(P3_device, "R2",     &P3_state[RP_R2],    ITEM_ID_BUTTON7,       generic_button_get_state);
      input_device_item_add(P3_device, "L2",     &P3_state[RP_L2],    ITEM_ID_BUTTON8,       generic_button_get_state);
      input_device_item_add(P3_device, "R3",     &P3_state[RP_R3],    ITEM_ID_BUTTON9,       generic_button_get_state);
      input_device_item_add(P3_device, "L3",     &P3_state[RP_L3],    ITEM_ID_BUTTON10,      generic_button_get_state);

      input_device_item_add(P4_device, "B",      &P4_state[RP_B],     ITEM_ID_BUTTON1 ,      generic_button_get_state);
      input_device_item_add(P4_device, "A",      &P4_state[RP_A],     ITEM_ID_BUTTON2 ,      generic_button_get_state);
      input_device_item_add(P4_device, "Y",      &P4_state[RP_Y],     ITEM_ID_BUTTON3,       generic_button_get_state);
      input_device_item_add(P4_device, "X",      &P4_state[RP_X],     ITEM_ID_BUTTON4,       generic_button_get_state);
      input_device_item_add(P4_device, "R",      &P4_state[RP_R],     ITEM_ID_BUTTON5,       generic_button_get_state);
      input_device_item_add(P4_device, "L",      &P4_state[RP_L],     ITEM_ID_BUTTON6,       generic_button_get_state);
      input_device_item_add(P4_device, "R2",     &P4_state[RP_R2],    ITEM_ID_BUTTON7,       generic_button_get_state);
      input_device_item_add(P4_device, "L2",     &P4_state[RP_L2],    ITEM_ID_BUTTON8,       generic_button_get_state);
      input_device_item_add(P4_device, "R3",     &P4_state[RP_R3],    ITEM_ID_BUTTON9,       generic_button_get_state);
      input_device_item_add(P4_device, "L3",     &P4_state[RP_L3],    ITEM_ID_BUTTON10,      generic_button_get_state);
   }

   retrokbd_device = input_device_add(machine, DEVICE_CLASS_KEYBOARD, "Retrokdb", NULL);

   if (retrokbd_device == NULL)
      fatalerror("KBD Error creating keyboard device\n");

   for (i = 0; i < RETROK_LAST; i++)
   {
      retrokbd_state[i] = 0;
      retrokbd_state2[i] = 0;
   }

   i = 0;
   do
   {
      input_device_item_add_kbd(retrokbd_device,\
            ktable[i].mame_key_name, &retrokbd_state[ktable[i].retro_key_name], ktable[i].mame_key, retrokbd_get_state);
      i++;
   } while (ktable[i].retro_key_name != -1);

}

void retro_poll_mame_input()
{
   input_poll_cb();

   // process_keyboard_state
   /* TODO: handle mods:SHIFT/CTRL/ALT/META/NUMLOCK/CAPSLOCK/SCROLLOCK */

   unsigned i = 0;
   do
   {
      retrokbd_state[ktable[i].retro_key_name] = input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, ktable[i].retro_key_name) ? 0x80 : 0;

      if (retrokbd_state[ktable[i].retro_key_name] && retrokbd_state2[ktable[i].retro_key_name] == 0)
      {
         ui_ipt_pushchar = ktable[i].retro_key_name;
         retrokbd_state2[ktable[i].retro_key_name] = 1;
      }
      else if (!retrokbd_state[ktable[i].retro_key_name] && retrokbd_state2[ktable[i].retro_key_name] == 1)
         retrokbd_state2[ktable[i].retro_key_name] = 0;

      i++;
   } while (ktable[i].retro_key_name != -1);

   if (mouse_enable)
   {
      static int mbL = 0, mbR = 0;
      int mouse_l;
      int mouse_r;
      int16_t mouse_x;
      int16_t mouse_y;

      mouse_x = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
      mouse_y = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
      mouse_l = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
      mouse_r = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
      mouseLX = mouse_x*INPUT_RELATIVE_PER_PIXEL;;
      mouseLY = mouse_y*INPUT_RELATIVE_PER_PIXEL;;

      if (mbL == 0 && mouse_l)
      {
         mbL = 1;
         mouseBUT[0] = 0x80;
      }
      else if (mbL == 1 && !mouse_l)
      {
         mouseBUT[0] = 0;
         mbL = 0;
      }

      if (mbR == 0 && mouse_r)
      {
         mbR = 1;
         mouseBUT[1] = 0x80;
      }
      else if(mbR == 1 && !mouse_r)
      {
         mouseBUT[1] = 0;
         mbR = 0;
      }
   }

   P1_state[RP_UP]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   P1_state[RP_DOWN]  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   P1_state[RP_LEFT]  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   P1_state[RP_RIGHT] = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   P1_state[RP_B]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   P1_state[RP_A]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   P1_state[RP_Y]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   P1_state[RP_X]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   P1_state[RP_L]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   P1_state[RP_R]     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   P1_state[RP_L2]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   P1_state[RP_R2]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   P1_state[RP_L3]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
   P1_state[RP_R3]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
   P1_state[RP_ST]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   P1_state[RP_SL]    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   P1_state[RP_LX]    = 2 * (input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X));
   P1_state[RP_LY]    = 2 * (input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y));
   P1_state[RP_RX]    = 2 * (input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
   P1_state[RP_RY]    = 2 * (input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));

   P2_state[RP_UP]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   P2_state[RP_DOWN]  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   P2_state[RP_LEFT]  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   P2_state[RP_RIGHT] = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   P2_state[RP_B]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   P2_state[RP_A]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   P2_state[RP_Y]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   P2_state[RP_X]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   P2_state[RP_L]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   P2_state[RP_R]     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   P2_state[RP_L2]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   P2_state[RP_R2]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   P2_state[RP_L3]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
   P2_state[RP_R3]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
   P2_state[RP_ST]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   P2_state[RP_SL]    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   P2_state[RP_LX]    = 2 * (input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X));
   P2_state[RP_LY]    = 2 * (input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y));
   P2_state[RP_RX]    = 2 * (input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
   P2_state[RP_RY]    = 2 * (input_state_cb(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));

   P3_state[RP_UP]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   P3_state[RP_DOWN]  = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   P3_state[RP_LEFT]  = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   P3_state[RP_RIGHT] = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   P3_state[RP_B]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   P3_state[RP_A]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   P3_state[RP_Y]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   P3_state[RP_X]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   P3_state[RP_L]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   P3_state[RP_R]     = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   P3_state[RP_L2]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   P3_state[RP_R2]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   P3_state[RP_L3]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
   P3_state[RP_R3]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
   P3_state[RP_ST]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   P3_state[RP_SL]    = input_state_cb(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   P3_state[RP_LX]    = 2 * (input_state_cb(2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X));
   P3_state[RP_LY]    = 2 * (input_state_cb(2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y));
   P3_state[RP_RX]    = 2 * (input_state_cb(2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
   P3_state[RP_RY]    = 2 * (input_state_cb(2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));

   P4_state[RP_UP]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   P4_state[RP_DOWN]  = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   P4_state[RP_LEFT]  = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   P4_state[RP_RIGHT] = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   P4_state[RP_B]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   P4_state[RP_A]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   P4_state[RP_Y]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   P4_state[RP_X]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   P4_state[RP_L]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   P4_state[RP_R]     = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   P4_state[RP_L2]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
   P4_state[RP_R2]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
   P4_state[RP_L3]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
   P4_state[RP_R3]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
   P4_state[RP_ST]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   P4_state[RP_SL]    = input_state_cb(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   P4_state[RP_LX]    = 2 * (input_state_cb(3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X));
   P4_state[RP_LY]    = 2 * (input_state_cb(3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y));
   P4_state[RP_RX]    = 2 * (input_state_cb(3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
   P4_state[RP_RY]    = 2 * (input_state_cb(3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));
}



//============================================================
//  main
//============================================================

static const char* xargv[] = {
	"mamemini",
	"-joystick",
	"-noautoframeskip",
	"-samplerate",
	"48000",
	"-sound",
	"-contrast",
	"1.0",
	"-brightness",
	"1.0",
	"-gamma",
	"1.0",
	"-rompath",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static int parsePath(char* path, char* gamePath, char* gameName) {
	int i;
	int slashIndex = -1;
	int dotIndex = -1;
	int len = strlen(path);
	if (len < 1) {
		return 0;
	}

	for (i = len - 1; i >=0; i--) {
		if (path[i] == path_default_slash()[0]) {
			slashIndex = i;
			break;
		} else
		if (path[i] == '.') {
			dotIndex = i;
		}
	}
	if (slashIndex < 0 && dotIndex >0){
		strcpy(gamePath, ".\0");
		strncpy(gameName, path , dotIndex );
		gameName[dotIndex] = 0;
		retro_log(RETRO_LOG_INFO, "[MAME 2010] path=%s gamePath=%s gameName=%s\n", path, gamePath, gameName);
		return 1;
	}
	if (slashIndex < 0 || dotIndex < 0) {
		return 0;
	}

	strncpy(gamePath, path, slashIndex);
	gamePath[slashIndex] = 0;
	strncpy(gameName, path + (slashIndex + 1), dotIndex - (slashIndex + 1));
	gameName[dotIndex - (slashIndex + 1)] = 0;

	retro_log(RETRO_LOG_INFO, "[MAME 2010] path=%s gamePath=%s gameName=%s\n", path, gamePath, gameName);
	return 1;
}

static int getGameInfo(char* gameName, int* rotation, int* driverIndex) {
	int gameFound = 0;
	int drvindex;

	//check invalid game name
	if (gameName[0] == 0)
		return 0;

	for (drvindex = 0; drivers[drvindex]; drvindex++) {
		if ( (drivers[drvindex]->flags & GAME_NO_STANDALONE) == 0 &&
			mame_strwildcmp(gameName, drivers[drvindex]->name) == 0 ) {
				gameFound = 1;
				*driverIndex = drvindex;
				*rotation = drivers[drvindex]->flags & 0x7;
				retro_log(RETRO_LOG_INFO, "[MAME 2010] %-18s\"%s\" rot=%i \n", drivers[drvindex]->name, drivers[drvindex]->description, *rotation);
		}
	}
	return gameFound;
}

int executeGame(char* path) {
	// cli_frontend does the heavy lifting; if we have osd-specific options, we
	// create a derivative of cli_options and add our own

	int paramCount;
	int result = 0;
	int gameRot=0;

	int driverIndex;

	FirstTimeUpdate = 1;

	screenRot = 0;

	//split the path to directory and the name without the zip extension
	result = parsePath(path, MgamePath, MgameName);
	if (result == 0) {
		retro_log(RETRO_LOG_ERROR, "[MAME 2010] Parse path failed! path=%s\n", path);
		strcpy(MgameName,path);
	//	return -1;
	}

	//Find the game info. Exit if game driver was not found.
	if (getGameInfo(MgameName, &gameRot, &driverIndex) == 0) {
		retro_log(RETRO_LOG_ERROR, "[MAME 2010] Game not found: %s\n", MgameName);
		return -2;
	}

	//tate enabled
	if (tate) {
		//horizontal game
		if (gameRot == ROT0) {
			screenRot = 1;
		} else
		if (gameRot &  ORIENTATION_FLIP_X) {
			retro_log(RETRO_LOG_INFO, "[MAME 2010]  *********** flip X\n");
			screenRot = 3;
		}

	} else
	{
		if (gameRot != ROT0) {
			screenRot = 1;
			if (gameRot &  ORIENTATION_FLIP_X) {
				retro_log(RETRO_LOG_INFO, "[MAME 2010]  *********** flip X\n");
				screenRot = 2;
			}
		}
	}

	retro_log(RETRO_LOG_INFO, "[MAME 2010] Creating frontend... game=%s\n", MgameName);

	//find how many parameters we have
	for (paramCount = 0; xargv[paramCount] != NULL; paramCount++);

	xargv[paramCount++] = (char*)libretro_content_directory;

	if (tate) {
		if (screenRot == 3) {
			xargv[paramCount++] =(char*) "-rol";
		} else {
			xargv[paramCount++] = (char*)(screenRot ? "-mouse" : "-ror");
		}
	} else {
		if (screenRot == 2) {
			xargv[paramCount++] = (char*)"-rol";
		} else {
			xargv[paramCount++] = (char*)(screenRot ? "-ror" : "-mouse");
		}
	}

	if(hide_gameinfo) {
		xargv[paramCount++] =(char*) "-skip_gameinfo";
	}

	if(hide_nagscreen) {
		xargv[paramCount++] =(char*) "-skip_nagscreen";
	}

	if(hide_warnings) {
		xargv[paramCount++] =(char*) "-skip_warnings";
	}

	xargv[paramCount++] = MgameName;

	retro_log(RETRO_LOG_INFO, "[MAME 2010] Invoking MAME2010 CLI frontend. Parameter count: %i\n", paramCount);

    char parameters[1024];
	for (int i = 0; xargv[i] != NULL; i++)
 		snprintf(parameters, sizeof(parameters), "%s ",xargv[i]);

    retro_log(RETRO_LOG_INFO, "[MAME 2010] Parameter list: %s\n", parameters);

	result = cli_execute(paramCount,(char**) xargv, NULL);

	xargv[paramCount - 2] = NULL;

	return result;
}



void osd_customize_input_type_list(input_type_desc *typelist)
{
// add this functionality back for any changes needed

}
//============================================================
//  mmain
//============================================================

#ifdef __cplusplus
extern "C"
#endif
int mmain(int argc, const char *argv)
{
	static char gameName[1024];

	strncpy(gameName, argv, 1023);
	if(executeGame(gameName)!=0) return -1;
	return 1;
}
