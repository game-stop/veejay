#define LGDMIC_CMD_LEN 1024


class lvdgmic
{
	private:
		gmic_list<float> images; 
		gmic_list<char> image_names;
		gmic	gmic_instance;
		int	format;

	public:
		lvdgmic( int n );
		~lvdgmic();

		char *buf;

		void push( int w, int h, int fmt, unsigned char **data, int n );
		void gmic_command( char const *str );
		void pull(int n, unsigned char **data);
};

