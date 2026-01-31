/*
  us

  Subroutine collection for assisting serial port input.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#pragma once

void us_rx_flush(void){
  delay(50);
  while (Serial.available()) Serial.read();
}

void us_waitforconnect (void){
  while (!Serial) {
    delay(50);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  digitalWrite(LED_BUILTIN, false);
}

// RECEIVE 1 BYTE CHAR
char us_getc (void) {
  while (!Serial.available()) ;
  return Serial.read();
}

int us_gets (char *s, int len) {
  int i = 0;
  char c;

  us_rx_flush();
  while (1) {
    c = us_getc();
    if ((c == '\n') || (c == '\r')) {
      s[i++] = '\0';
      Serial.print("\r\n");
      break;
    } else if ((i > 0) && (c == '\b')) {
      Serial.write ("\b \b");
      i--;
    } else if ((i < len) && (i >= 0) && (c >= ' ') && (c <= '~')) {
      s[i++] = (char)c;
      Serial.write (c);
    } else Serial.write ('\a');
  }
  return i;
}

bool are_you_sure(void) {
  us_rx_flush();
  Serial.printf("Are you sure? (y/n) ");
  while (!Serial.available()) ;
  char c = Serial.read();
  us_rx_flush();
  Serial.println(c);
  return (c == 'y' || c == 'Y');
}

String pass(const char *s) {
  String p;
  for (int i = 0; i < strlen(s); i++) {
    p += '*';
  }
  return p;
}
