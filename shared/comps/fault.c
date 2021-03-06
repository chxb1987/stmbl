#include "commands.h"
#include "hal.h"
#include "math.h"
#include "defines.h"
#include "angle.h"
#include "main.h"

HAL_COMP(fault);

HAL_PIN(en);
HAL_PIN(state);
HAL_PIN(fault);
HAL_PIN(en_out);
HAL_PIN(en_pid);

HAL_PIN(phase_start);
HAL_PIN(phase_ready);

HAL_PIN(cmd);
HAL_PIN(fb);
HAL_PIN(start_offset);

HAL_PIN(cmd_error);
HAL_PIN(mot_fb_error);
HAL_PIN(com_fb_error);
HAL_PIN(joint_fb_error);
HAL_PIN(hv_error);

HAL_PIN(hv_temp);
HAL_PIN(mot_temp);
HAL_PIN(max_hv_temp);
HAL_PIN(max_mot_temp);
HAL_PIN(high_hv_temp);
HAL_PIN(high_mot_temp);
HAL_PIN(fan_hv_temp);
HAL_PIN(fan_mot_temp);

HAL_PIN(scale);

HAL_PIN(dc_volt);
HAL_PIN(min_dc_volt);
HAL_PIN(high_dc_volt);
HAL_PIN(max_dc_volt);

HAL_PIN(dc_cur);
HAL_PIN(high_dc_cur);
HAL_PIN(max_dc_cur);

HAL_PIN(pos_error);
HAL_PIN(max_pos_error);

HAL_PIN(sat);
HAL_PIN(max_sat);

HAL_PIN(mot_brake);
HAL_PIN(dc_brake);

HAL_PIN(hv_fan);
HAL_PIN(mot_fan);

HAL_PIN(phase_with_brake);
HAL_PIN(phase_on_start);
HAL_PIN(rephase);

HAL_PIN(print);

HAL_PIN(brake_release);

struct fault_ctx_t{
   state_t state;
   fault_t fault;
   uint32_t phased;
   float cmd_error;
   float mot_fb_error;
   float com_fb_error;
   float joint_fb_error;
   float hv_error;
   float hv_temp_error;
   float dc_volt_error;
   float mot_temp_error;
};

static void nrt_init(volatile void * ctx_ptr, volatile hal_pin_inst_t * pin_ptr){
   struct fault_ctx_t * ctx = (struct fault_ctx_t *)ctx_ptr;
   struct fault_pin_ctx_t * pins = (struct fault_pin_ctx_t *)pin_ptr;
   ctx->state = DISABLED;
   ctx->fault = NO_ERROR;
   ctx->phased = 0;
   PIN(phase_with_brake) = 1.0;
   PIN(phase_on_start) = 1.0;
   PIN(min_dc_volt) = 20.0;
   PIN(high_dc_volt) = 370.0;
   PIN(max_dc_volt) = 390.0;
   PIN(max_hv_temp) = 90.0;
   PIN(max_mot_temp) = 100.0;
   PIN(high_hv_temp) = 70.0;
   PIN(high_mot_temp) = 80.0;
   PIN(fan_hv_temp) = 60.0;
   PIN(fan_mot_temp) = 60.0;
}

float err_filter(float *ctx, float max, float dens, float err){
  if(err > 0.0){
    *ctx += 1.0;
  }
  else{
    *ctx -= dens;
  }

  *ctx = CLAMP(*ctx, 0.0, max);

  if(*ctx < max * 0.99){
    return(0.0);
  }
  return(1.0);
}


static void rt_func(float period, volatile void * ctx_ptr, volatile hal_pin_inst_t * pin_ptr){
   struct fault_ctx_t * ctx = (struct fault_ctx_t *)ctx_ptr;
   struct fault_pin_ctx_t * pins = (struct fault_pin_ctx_t *)pin_ptr;

   if(PIN(phase_on_start) <= 0.0){
      ctx->phased = 1;
   }
   
   switch(ctx->state){
      case DISABLED:
         if(PIN(en) > 0.0){
            if(PIN(rephase) > 0.0){ // TODO: check phase_on_start
               ctx->phased = 0;
            }
            if(ctx->phased == 0){
               ctx->state = PHASING;
            }
            else{
               ctx->state = ENABLED;
               PIN(start_offset) = minus(PIN(fb), PIN(cmd));
            }
         }      
      break;
      
      case ENABLED:
         if(PIN(en) <= 0.0){
            ctx->state = DISABLED;
         }
      break;
         
      case PHASING:
         if(RISING_EDGE(PIN(phase_ready))){
            ctx->state = ENABLED;
            PIN(start_offset) = minus(PIN(fb), PIN(cmd));
         }
         
         if(PIN(en) <= 0.0){
            ctx->state = DISABLED;
         }
      break;
         
      case SOFT_FAULT:
         if(PIN(en) <= 0.0){
            ctx->state = DISABLED;
         }
      break;
      
      case LED_TEST:
      case HARD_FAULT:
      break;
   }

   if(err_filter(&(ctx->cmd_error), 5.0, 0.001, PIN(cmd_error) > 0.0)){
      ctx->fault = CMD_ERROR;
      ctx->state = SOFT_FAULT;
   }

   if(err_filter(&(ctx->mot_fb_error), 5.0, 0.001, PIN(mot_fb_error) > 0.0)){
      ctx->fault = MOT_FB_ERROR;
      ctx->state = SOFT_FAULT;
      ctx->phased = 0;
   }

   if(err_filter(&(ctx->com_fb_error), 5.0, 0.001, PIN(com_fb_error) > 0.0)){
      ctx->fault = COM_FB_ERROR;
      ctx->state = SOFT_FAULT;
      ctx->phased = 0;
   }
   
   if(err_filter(&(ctx->joint_fb_error), 5.0, 0.001, PIN(joint_fb_error) > 0.0)){
      ctx->fault = JOINT_FB_ERROR;
      ctx->state = SOFT_FAULT;
      ctx->phased = 0;
   }

   if(err_filter(&(ctx->hv_error), 3.0, 0.001, PIN(hv_error) > 0.0)){
      ctx->fault = HV_ERROR;
      ctx->state = SOFT_FAULT;
   }

   if(ABS(PIN(pos_error)) > PIN(max_pos_error)){
      ctx->fault = POS_ERROR;
      ctx->state = SOFT_FAULT;
   }
   
   if(PIN(sat) > PIN(max_sat)){
      ctx->fault = SAT_ERROR;
      ctx->state = SOFT_FAULT;
   }

   if(err_filter(&(ctx->hv_temp_error), 5.0, 0.001, PIN(hv_temp) > PIN(max_hv_temp))){
      ctx->fault = HV_TEMP_ERROR;
      ctx->state = SOFT_FAULT;
   }

   if(err_filter(&(ctx->dc_volt_error), 5.0, 0.001, PIN(dc_volt) > PIN(max_dc_volt) || PIN(dc_volt) < PIN(min_dc_volt))){
      ctx->fault = HV_VOLT_ERROR;
      ctx->state = SOFT_FAULT;
   }

   if(err_filter(&(ctx->mot_temp_error), 5.0, 0.001, PIN(mot_temp) > PIN(max_mot_temp))){
      ctx->fault = MOT_TEMP_ERROR;
      ctx->state = SOFT_FAULT;
   }

   float scale = 1.0;
   scale = MIN(scale, SCALE(PIN(hv_temp), PIN(high_hv_temp), PIN(max_hv_temp)));
   scale = MIN(scale, SCALE(PIN(dc_volt), PIN(high_dc_volt), PIN(max_dc_volt)));
   scale = MIN(scale, SCALE(PIN(mot_temp), PIN(high_mot_temp), PIN(max_mot_temp)));
   scale = MIN(scale, SCALE(PIN(dc_cur), PIN(max_dc_cur) * 0.8, PIN(max_dc_cur))); // TODO PIN(high_dc_cur)

   PIN(dc_brake) = SCALE(PIN(dc_volt), PIN(max_dc_volt), PIN(high_dc_volt));

   if(PIN(hv_temp) >= PIN(fan_hv_temp)){
      PIN(hv_fan) = 1.0;
   }

   if(PIN(hv_temp) < PIN(fan_hv_temp) * 0.9){
      PIN(hv_fan) = 0.0;
   }

   if(PIN(mot_temp) >= PIN(fan_mot_temp)){
      PIN(mot_fan) = 1.0;
   }

   if(PIN(mot_temp) < PIN(fan_mot_temp) * 0.9){
      PIN(mot_fan) = 0.0;
   }

   switch(ctx->state){
      case DISABLED:
         PIN(phase_start) = 0.0;
         PIN(mot_brake) = 0.0;
         PIN(en_out) = 0.0;
         PIN(en_pid) = 0.0;
         ctx->fault = NO_ERROR;
         // scale = 0.0;
      break;
      
      case ENABLED:
         PIN(phase_start) = 0.0;
         PIN(mot_brake) = 1.0;
         PIN(en_out) = 1.0;
         PIN(en_pid) = 1.0;
         ctx->fault = NO_ERROR;
         ctx->phased = 1;
      break;
      
      case PHASING:
         PIN(phase_start) = 1.0;
         PIN(mot_brake) = PIN(phase_with_brake);
         ctx->fault = NO_ERROR;
         PIN(en_pid) = 0.0;
         PIN(en_out) = 1.0;
      break;
      
      case SOFT_FAULT:
         PIN(phase_start) = 0.0;
         PIN(mot_brake) = 0.0;
         PIN(en_out) = 0.0;
         PIN(en_pid) = 0.0;
         // scale = 0.0;
      break;
      
      case LED_TEST:
      case HARD_FAULT:
         PIN(phase_start) = 0.0;
         PIN(mot_brake) = 0.0;
         PIN(en_out) = 0.0;
         PIN(en_pid) = 0.0;
         // scale = 0.0;
      break;
   }

   PIN(fault) = ctx->fault;
   PIN(state) = ctx->state;
   PIN(scale) = scale;
   
   if(PIN(brake_release) > 0.0){
      PIN(mot_brake) = 1.0;
   }
}


static void nrt_func(volatile void * ctx_ptr, volatile hal_pin_inst_t * pin_ptr){
struct fault_ctx_t * ctx = (struct fault_ctx_t *)ctx_ptr;
struct fault_pin_ctx_t * pins = (struct fault_pin_ctx_t *)pin_ptr;
//TODO: fix EDGE
if(EDGE(ctx->state) || PIN(print) > 0.0){
   PIN(print) = 0.0;
   switch((state_t)ctx->state){
      case DISABLED:
      printf("INFO: Disabled \n");
      break;

      case ENABLED:
      printf("INFO: Enabled \n");
      break;

      case PHASING:
      printf("INFO: Phasing \n");
      break;

      case SOFT_FAULT:
      printf("ERROR: Soft fault: ");
      switch((fault_t)ctx->fault){
         case NO_ERROR:
         printf("no error\n");
         break;
        
         case CMD_ERROR:
         printf("CMD error\n");
         break;
        
         case MOT_FB_ERROR:
         printf("mot FB error\n");
         break;
        
         case COM_FB_ERROR:
         printf("com FB error\n");
         break;
         
         case JOINT_FB_ERROR:
         printf("com FB error\n");
         break;
        
         case POS_ERROR:
         printf("position error\n");
         break;
       
         case SAT_ERROR:
         printf("saturation error\n");
         break;
        
         case HV_ERROR:
         printf("HV error\n");
         break;
        
         case HV_TEMP_ERROR:
         printf("HV overtemperture\n");
         break;
        
         case HV_VOLT_ERROR:
         printf("HV volt error\n");
         break;
        
         case MOT_TEMP_ERROR:
         printf("Motor overtemperture\n");
         break;
      }
      break;

      case HARD_FAULT:
      printf("ERROR: Hard fault: \n");
      break;
    
      default:
      break;
   }
}
}

hal_comp_t fault_comp_struct = {
  .name = "fault",
  .nrt = nrt_func,
  .rt = rt_func,
  .frt = 0,
  .nrt_init = nrt_init,
  .rt_start = 0,
  .frt_start = 0,
  .rt_stop = 0,
  .frt_stop = 0,
  .ctx_size = sizeof(struct fault_ctx_t),
  .pin_count = sizeof(struct fault_pin_ctx_t) / sizeof(struct hal_pin_inst_t),
};
