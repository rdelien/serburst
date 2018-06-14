/*****************************************************************************/
/***   _ __     __ _  _     _  _      _					   ***/
/***  | |\ \   / /| || | = | || \    / |				   ***/
/***  | | \ \ / / | || | = | ||  \  /  |				   ***/
/***  | |  \   /  | || |___| || |\\//| |				   ***/
/***  |_|   \_/   |_| \_____/ |_|    |_|				   ***/
/***									   ***/
/*** File:	termios_conv.h						   ***/
/*** Date:	12-07-2015						   ***/
/*** Developer:	Robert Delien (robert@delien.nl)			   ***/
/*** Copyright:	(c)2016, Ivium						   ***/
/***									   ***/
/*** Function:	This file contains the interface for termios.c.		   ***/
/***									   ***/
/*****************************************************************************/
#ifndef TERMIOS_CONV_H				/* Include file already compiled? */
#define TERMIOS_CONV_H


speed_t termios_bitrate(unsigned long const bitrate);


#endif /* TERMIOS_CONV_H */
