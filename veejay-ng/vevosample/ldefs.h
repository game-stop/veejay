#ifndef LOCALDEFS
#define LOCALDEFS
#include <stdint.h>
#define	BIND_OUT_P 0
#define BIND_IN_P 1
#define BIND_ENTRY  2
//! \typedef sampleinfo_t Sample A/V Information structure
typedef struct
{
	uint64_t	start_pos;	//!< Starting position 
	uint64_t	end_pos;	//!< Ending position 
	int		looptype;	//!< Looptype
	int		speed;		//!< Playback speed
	int		repeat;	
	uint64_t	in_point;	//!< In point (overrides start_pos)
	uint64_t	out_point;	//!< Out point (overrides end_pos)
	uint64_t	current_pos;	//!< Current position
	int		marker_lock;	//!< Keep in-out point length constant
	int		rel_pos;	//!< Relative position
	int		has_audio;	//!< Audio available
	int		repeat_count;
	int		type;		//!< Type of Sample
	uint64_t	rate;		//!< AudioRate
	double		fps;		//!< Frame rate of Sample
	int		bps;
	int		bits;
	int		channels;
	double		rec;		//!< Rec. percentage done
} sampleinfo_t;


typedef struct
{
	int 	id;
	int	active;
	int	fx_id;
	void    *fx_osc;
	void	*fx_instance;
	void	*in_values;
	void	*out_values;
	void    *in_channels;
	void	*out_channels;
	void	*bind;
	char	*window;
	char	*frame;
	char    *subwindow;
	char    *subframe;
	double   alpha;
} fx_slot_t;

typedef struct
{
	double min[2];
	double max[2];
	int    p[3];
	int    kind;
} bind_parameter_t;


typedef struct
{
	int	rec;
	int	con;
	int	max_size;
	int	format;
	char	aformat;
	void	*fd;
	long 	tf;
	long	nf;
	uint8_t *buf;
	void    *codec;
} samplerecord_t;

//! \typedef sample_runtime_data Sample Runtime Data structure
typedef	struct
{
	void	*data;						/* private data, depends on stream type */
	void	*info_port;					/* collection of sample properties */
	int	width;						/* processing information */
	int	height;
	int	format;
	int	palette;
	int	type;						/* type of sample */
	samplerecord_t *record;
	sampleinfo_t *info;
	void	*osc;
	void	*user_data;
	void    *mapping;
	void	*rmapping;
	void	*bundle;
	int	primary_key;
	void	*fmt_port;
} sample_runtime_data;

char		*sample_translate_property( void *sample, char *name );


#endif
