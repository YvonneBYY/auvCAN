#ifndef APP_H
#define APP_H

/*
 * Call from the CubeMX-generated main.c:
 *
 *   USER CODE BEGIN 2      ->  app_setup();
 *   USER CODE BEGIN WHILE  ->  while (1) { app_loop(); }
 */
void app_setup(void);
void app_loop(void);

#endif
