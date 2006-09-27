#ifndef ANIM_UI_H
#define ANIM_UI_H

void	anim_ui_new(void *sender, char *path, char *types);

void	anim_ui_collection_init();

void	anim_ui_collection_destroy();
void	anim_ui_collection_free();

void	anim_ui_collection_bang( double pos );

#endif
