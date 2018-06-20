#pragma once

typedef enum ETRAP_LOGLEVEL_tag
{
	/**
	 * All message levels are prohibited.
	 */
	LV_NONE      = 0,	
    /**
	 * Fatal error message. 
	 */
	LV_FATAL = 1,
	/**
	 * Error message.
	 */
	LV_ERROR     = 2,
	/**
	 * Waning message.
	 */
	LV_WARNING   = 3,
	/**
	 * information message.
	 */
	LV_INFO      = 4,
}ETRAP_LOGLEVEL;