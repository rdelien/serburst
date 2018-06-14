/*****************************************************************************/
/***   _ __     __ _  _     _  _      _					   ***/
/***  | |\ \   / /| || | = | || \    / |				   ***/
/***  | | \ \ / / | || | = | ||  \  /  |				   ***/
/***  | |  \   /  | || |___| || |\\//| |				   ***/
/***  |_|   \_/   |_| \_____/ |_|    |_|				   ***/
/***									   ***/
/*** File:	termios_conv.c						   ***/
/*** Date:	12-07-2015						   ***/
/*** Developer:	Robert Delien (robert@delien.nl)			   ***/
/*** Copyright:	(c)2016, Ivium						   ***/
/***									   ***/
/*** Function:	This file contains conversions termios.			   ***/
/***									   ***/
/*****************************************************************************/
#include <termios.h>


speed_t termios_bitrate(unsigned long const bitrate)
{
	switch (bitrate) {
#if defined (B0)
	case 0:
		return B0;
#endif
#if defined (B50)
	case 50:
		return B50;
#endif
#if defined (B75)
	case 75:
		return B75;
#endif
#if defined (B110)
	case 110:
		return B110;
#endif
#if defined (B134)
	case 134:
		return B134;
#endif
#if defined (B150)
	case 150:
		return B150;
#endif
#if defined (B200)
	case 200:
		return B200;
#endif
#if defined (B300)
	case 300:
		return B300;
#endif
#if defined (B600)
	case 600:
		return B600;
#endif
#if defined (B1200)
	case 1200:
		return B1200;
#endif
#if defined (B1800)
	case 1800:
		return B1800;
#endif
#if defined (B2400)
	case 2400:
		return B2400;
#endif
#if defined (B4800)
	case 4800:
		return B4800;
#endif
#if defined (B9600)
	case 9600:
		return B9600;
#endif
#if defined (B19200)
	case 19200:
		return B19200;
#endif
#if defined (B38400)
	case 38400:
		return B38400;
#endif
#if defined (B57600)
	case 57600:
		return B57600;
#endif
#if defined (B115200)
	case 115200:
		return B115200;
#endif
#if defined (B230400)
	case 230400:
		return B230400;
#endif
#if defined (B460800)
	case 460800:
		return B460800;
#endif
	default:
		/* Unsupported bit rate */
		break;
	}
	return 0;
}
