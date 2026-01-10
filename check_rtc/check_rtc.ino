#include "hardware/rtc.h"

datetime_t currTime = { 2026, 1, 4, 0, 11, 0, 0 };

void setup() {
  // put your setup code here, to run once:
  set_sys_clock_khz(4*12000, true);

  while(!Serial);
  Serial.print(F(__DATE__)); Serial.print(' '); Serial.print(F( __TIME__)); Serial.println();
  Serial.print("rtc running "); Serial.println(rtc_running());
  rtc_init();
  rtc_set_datetime(&currTime);
  Serial.print("rtc running "); Serial.println(rtc_running());

}

void printDatetime(const char *txt, datetime_t *t)
{
  Serial.printf("%s: %04d-%02d-%02d ",txt, t->year,t->month,t->day);
  Serial.printf("%02d:%02d:%02d\n",t->hour,t->min,t->sec);
}
void loop() {
  // put your main code here, to run repeatedly:
  rtc_get_datetime(&currTime);
  printDatetime("rtc",&currTime);
  delay(1000);
}
