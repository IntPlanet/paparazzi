/*
 * Paparazzi $Id$
 *  
 * Copyright (C) 2006  Pascal Brisset, Antoine Drouin, Michel Gorraz
 *
 * This file is part of paparazzi.
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 *
 */

/** 
 *
 * fixed wing horizontal control
 *
 */

#include "fw_h_ctl.h"
#include "estimator.h"
#include "nav.h"
#include "airframe.h"
#include "fw_v_ctl.h"


/* outer loop parameters */
float h_ctl_course_setpoint;
float h_ctl_course_pre_bank;
float h_ctl_course_pre_bank_correction;
float h_ctl_course_pgain;
float h_ctl_roll_max_setpoint;

/* inner roll loop parameters */
float  h_ctl_roll_setpoint;
float  h_ctl_roll_pgain;
pprz_t h_ctl_aileron_setpoint;

/* inner pitch loop parameters */
float  h_ctl_pitch_setpoint;
float  h_ctl_pitch_pgain;
pprz_t h_ctl_elevator_setpoint;

/* inner loop pre-command */
float h_ctl_aileron_of_throttle;
float h_ctl_elevator_of_roll;

/* rate loop */
#ifdef H_CTL_RATE_LOOP
float h_ctl_roll_rate_setpoint;
float h_ctl_roll_rate_mode;
float h_ctl_roll_rate_setpoint_pgain;
float h_ctl_roll_rate_pgain;
float h_ctl_roll_rate_igain;
float h_ctl_roll_rate_dgain;
#endif

inline static void h_ctl_roll_loop( void );
inline static void h_ctl_pitch_loop( void );
#ifdef H_CTL_RATE_LOOP
static inline void h_ctl_roll_rate_loop( void );
#endif

#ifndef H_CTL_COURSE_PRE_BANK_CORRECTION
#define H_CTL_COURSE_PRE_BANK_CORRECTION 1.
#endif

void h_ctl_init( void ) {

  h_ctl_course_setpoint = 0.;
  h_ctl_course_pre_bank = 0.;
  h_ctl_course_pre_bank_correction = H_CTL_COURSE_PRE_BANK_CORRECTION;
  h_ctl_course_pgain = H_CTL_COURSE_PGAIN;
  h_ctl_roll_max_setpoint = H_CTL_ROLL_MAX_SETPOINT;

  h_ctl_roll_setpoint = 0.;
  h_ctl_roll_pgain = H_CTL_ROLL_PGAIN;
  h_ctl_aileron_setpoint = 0;
  h_ctl_aileron_of_throttle = H_CTL_AILERON_OF_THROTTLE;

  h_ctl_pitch_setpoint = 0.;
  h_ctl_pitch_pgain = H_CTL_PITCH_PGAIN;
  h_ctl_elevator_setpoint = 0;
  h_ctl_elevator_of_roll = H_CTL_ELEVATOR_OF_ROLL;

#ifdef H_CTL_RATE_LOOP
  h_ctl_roll_rate_mode = H_CTL_ROLL_RATE_MODE_DEFAULT;
  h_ctl_roll_rate_setpoint_pgain = H_CTL_ROLL_RATE_SETPOINT_PGAIN;
  h_ctl_roll_rate_pgain = H_CTL_ROLL_RATE_PGAIN;
  h_ctl_roll_rate_igain = H_CTL_ROLL_RATE_IGAIN;
  h_ctl_roll_rate_dgain = H_CTL_ROLL_RATE_DGAIN;
#endif
}

/** 
 * \brief 
 *
 */
void h_ctl_course_loop ( void ) {
  float err = estimator_hspeed_dir - h_ctl_course_setpoint;
  NormRadAngle(err);
  float speed_depend_nav = estimator_hspeed_mod/NOMINAL_AIRSPEED; 
  Bound(speed_depend_nav, 0.66, 1.5);
  float cmd = h_ctl_course_pgain * err * speed_depend_nav;
#if defined  AGR_CLIMB
  if (v_ctl_auto_throttle_submode == V_CTL_AUTO_THROTTLE_AGRESSIVE) {
    float altitude_error = estimator_z - v_ctl_altitude_setpoint;
    cmd *= ((altitude_error < 0) ? AGR_CLIMB_NAV_RATIO : AGR_DESCENT_NAV_RATIO);
  }
#endif
  h_ctl_roll_setpoint = cmd + h_ctl_course_pre_bank_correction * h_ctl_course_pre_bank;
  BoundAbs(h_ctl_roll_setpoint, h_ctl_roll_max_setpoint);
}

void h_ctl_attitude_loop ( void ) {
  h_ctl_roll_loop();
  h_ctl_pitch_loop();
}

inline static void h_ctl_roll_loop( void ) {
  float err = estimator_phi - h_ctl_roll_setpoint;
  float cmd = h_ctl_roll_pgain * err 
    + v_ctl_throttle_setpoint * h_ctl_aileron_of_throttle;
  h_ctl_aileron_setpoint = TRIM_PPRZ(cmd);

#ifdef H_CTL_RATE_LOOP
  h_ctl_roll_rate_setpoint = h_ctl_roll_rate_setpoint_pgain * err;
  BoundAbs(h_ctl_roll_rate_setpoint, H_CTL_ROLL_RATE_MAX_SETPOINT);

  float saved_aileron_setpoint = h_ctl_aileron_setpoint;
  h_ctl_roll_rate_loop();
  h_ctl_aileron_setpoint = Blend(h_ctl_aileron_setpoint, saved_aileron_setpoint, h_ctl_roll_rate_mode) ;
#endif
}

#ifdef H_CTL_RATE_LOOP
static inline void h_ctl_roll_rate_loop() {
  float err = estimator_p - h_ctl_roll_rate_setpoint;
  
  /* I term calculation */
  static float roll_rate_sum_err = 0.;
  static uint8_t roll_rate_sum_idx = 0;
  static float roll_rate_sum_values[H_CTL_ROLL_RATE_SUM_NB_SAMPLES];
  
  roll_rate_sum_err -= roll_rate_sum_values[roll_rate_sum_idx];
  roll_rate_sum_values[roll_rate_sum_idx] = err;
  roll_rate_sum_err += err;
  roll_rate_sum_idx++;
  if (roll_rate_sum_idx >= H_CTL_ROLL_RATE_SUM_NB_SAMPLES) roll_rate_sum_idx = 0;
  
  /* D term calculations */
  static float last_err = 0;
  float d_err = err - last_err;
  last_err = err;

  float cmd = h_ctl_roll_rate_pgain * ( err + h_ctl_roll_rate_igain * roll_rate_sum_err / H_CTL_ROLL_RATE_SUM_NB_SAMPLES + h_ctl_roll_rate_dgain * d_err);

  h_ctl_aileron_setpoint = TRIM_PPRZ(cmd);
}
#endif /* H_CTL_RATE_LOOP */

inline static void h_ctl_pitch_loop( void ) {
  /* sanity check */
  if (h_ctl_elevator_of_roll <0.)
    h_ctl_elevator_of_roll = 0.;
  float err = estimator_theta - h_ctl_pitch_setpoint;
  float cmd = err * h_ctl_pitch_pgain 
    + h_ctl_elevator_of_roll * fabs(estimator_phi);
#ifdef LOITER_TRIM
  cmd += loiter();
#endif
  h_ctl_elevator_setpoint = TRIM_PPRZ(cmd);
}





#ifdef LOITER_TRIM

float loiter_trim = V_CTL_AUTO_THROTTLE_MIN_CRUISE_THROTTLE;
float dash_trim = V_CTL_AUTO_THROTTLE_MAX_CRUISE_THROTTLE;

inline static void loiter(void) {
  static float last_elevator_trim = 0;
  float elevator_trim;
  Bound(v_ctl_auto_throttle_cruise_throttle, 
	V_CTL_AUTO_THROTTLE_MIN_CRUISE_THROTTLE, 
	V_CTL_AUTO_THROTTLE_MAX_CRUISE_THROTTLE);
  float cruise_trim = v_ctl_auto_throttle_cruise_throttle 
    - V_CTL_AUTO_THROTTLE_CRUISE_THROTTLE;
  float max_change = (loiter_trim-dash_trim)/80.;
  if (cruise_trim > 0) {
    elevator_trim = cruise_trim * dash_trim / (V_CTL_AUTO_THROTTLE_MAX_CRUISE_THROTTLE - V_CTL_AUTO_THROTTLE_CRUISE_THROTTLE);
    elevator_trim=Max(last_elevator_trim - max_change, elevator_trim);
  }
  else {
    elevator_trim = cruise_trim * loiter_trim / (V_CTL_AUTO_THROTTLE_CRUISE_THROTTLE- V_CTL_AUTO_THROTTLE_MIN_CRUISE_THROTTLE);
    elevator_trim=Min(last_elevator_trim+ max_change, elevator_trim);
  }
  last_elevator_trim = elevator_trim;
  return elevator_trim;
}
#endif

