/* Settings kept between launches in ux0:data/prism/prism.cfg, one key=value per
 * line. */
#ifndef CFG_H
#define CFG_H

void cfg_load(void);
const char *cfg_bank(void);           /* last bank file name; "" = built-in */
void cfg_set_bank(const char *name);  /* store and write the file */
int  cfg_ahead(void);                 /* audio blocks rendered ahead of the port; 3 by default */
void cfg_set_ahead(int blocks);
int  cfg_state(void);                 /* list index of the performance last saved or loaded; -1 = none */
void cfg_set_state(int idx);

#endif
