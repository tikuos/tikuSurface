/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * touch_key.h - characters of the panel's two touch keyboard pages.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TOUCH_KEY_H
#define SURFACE_TOUCH_KEY_H
/** @brief Return one ASCII key from the letters or symbols page. */
static inline char surface_touch_key(int index,int symbols,int shift)
{
    static const char letters[]="1234567890qwertyuiopasdfghjkl.zxcvbnm-_@";
    static const char punctuation[]="!@#$%^&*()[]{}<>/\\|?+=:;\"'`~_,1234567890";
    char c=symbols?punctuation[index]:letters[index];
    if(shift && c>='a' && c<='z') c=(char)(c-'a'+'A');
    return c;
}
#endif
