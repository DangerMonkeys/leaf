#ifndef MENU_PAGE_H
#define MENU_PAGE_H

class MenuPage {
  public:
    // Called whenever a button event occurs
    //   button: Button to which the event pertains (TODO: change to enum)
    //   state: New state of button (TODO: change to enum)
    //   count: (TODO: document)
    virtual bool button_event(uint8_t button, uint8_t state, uint8_t count) = 0;

    // Called to draw the menu page.
    // Assumes(?) the screen is already clear.
    virtual void draw() = 0;
};

#endif
