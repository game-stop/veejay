class lvdgmic
{
	private:
		gmic_list<float> images; 
		gmic_list<char> image_names;
		gmic	gmic_instance;

	public:
		lvdgmic( int n );
		~lvdgmic();

		void push( int w, int h, int fmt, unsigned char **data, int n );
		void gmic_command( char const *str );
		void pull(int n, unsigned char **data);
};

