/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "colortemp.h"

vj_effect *colortemp_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 781;
    ve->defaults[0] = 137;

	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;
	ve->defaults[1] = 1;

	ve->limits[0][2] = 0;
	ve->limits[1][2] = 255;
	ve->defaults[2] = 0;

    ve->description = "Color Temperature";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Temperature", "Automatic", "Opacity" );
    return ve;
}

static struct {
	int r;
	int g;
	int b;
} blackbody_t[] = {
    { 255 , 51 , 0 },
    { 255 , 56 , 0 },
    { 255 , 69 , 0 },
    { 255 , 71 , 0 },
    { 255 , 82 , 0 },
    { 255 , 83 , 0 },
    { 255 , 93 , 0 },
    { 255 , 93 , 0 },
    { 255 , 102 , 0 },
    { 255 , 101 , 0 },
    { 255 , 111 , 0 },
    { 255 , 109 , 0 },
    { 255 , 118 , 0 },
    { 255 , 115 , 0 },
    { 255 , 124 , 0 },
    { 255 , 121 , 0 },
    { 255 , 130 , 0 },
    { 255 , 126 , 0 },
    { 255 , 135 , 0 },
    { 255 , 131 , 0 },
    { 255 , 141 , 11 },
    { 255 , 137 , 18 },
    { 255 , 146 , 29 },
    { 255 , 142 , 33 },
    { 255 , 152 , 41 },
    { 255 , 147 , 44 },
    { 255 , 157 , 51 },
    { 255 , 152 , 54 },
    { 255 , 162 , 60 },
    { 255 , 157 , 63 },
    { 255 , 166 , 69 },
    { 255 , 161 , 72 },
    { 255 , 170 , 77 },
    { 255 , 165 , 79 },
    { 255 , 174 , 84 },
    { 255 , 169 , 87 },
    { 255 , 178 , 91 },
    { 255 , 173 , 94 },
    { 255 , 182 , 98 },
    { 255 , 177 , 101 },
    { 255 , 185 , 105 },
    { 255 , 180 , 107 },
    { 255 , 189 , 111 },
    { 255 , 184 , 114 },
    { 255 , 192 , 118 },
    { 255 , 187 , 120 },
    { 255 , 195 , 124 },
    { 255 , 190 , 126 },
    { 255 , 198 , 130 },
    { 255 , 193 , 132 },
    { 255 , 201 , 135 },
    { 255 , 196 , 137 },
    { 255 , 203 , 141 },
    { 255 , 199 , 143 },
    { 255 , 206 , 146 },
    { 255 , 201 , 148 },
    { 255 , 208 , 151 },
    { 255 , 204 , 153 },
    { 255 , 211 , 156 },
    { 255 , 206 , 159 },
    { 255 , 213 , 161 },
    { 255 , 209 , 163 },
    { 255 , 215 , 166 },
    { 255 , 211 , 168 },
    { 255 , 217 , 171 },
    { 255 , 213 , 173 },
    { 255 , 219 , 175 },
    { 255 , 215 , 177 },
    { 255 , 221 , 180 },
    { 255 , 217 , 182 },
    { 255 , 223 , 184 },
    { 255 , 219 , 186 },
    { 255 , 225 , 188 },
    { 255 , 221 , 190 },
    { 255 , 226 , 192 },
    { 255 , 223 , 194 },
    { 255 , 228 , 196 },
    { 255 , 225 , 198 },
    { 255 , 229 , 200 },
    { 255 , 227 , 202 },
    { 255 , 231 , 204 },
    { 255 , 228 , 206 },
    { 255 , 232 , 208 },
    { 255 , 230 , 210 },
    { 255 , 234 , 211 },
    { 255 , 232 , 213 },
    { 255 , 235 , 215 },
    { 255 , 233 , 217 },
    { 255 , 237 , 218 },
    { 255 , 235 , 220 },
    { 255 , 238 , 222 },
    { 255 , 236 , 224 },
    { 255 , 239 , 225 },
    { 255 , 238 , 227 },
    { 255 , 240 , 228 },
    { 255 , 239 , 230 },
    { 255 , 241 , 231 },
    { 255 , 240 , 233 },
    { 255 , 243 , 234 },
    { 255 , 242 , 236 },
    { 255 , 244 , 237 },
    { 255 , 243 , 239 },
    { 255 , 245 , 240 },
    { 255 , 244 , 242 },
    { 255 , 246 , 243 },
    { 255 , 245 , 245 },
    { 255 , 247 , 245 },
    { 255 , 246 , 248 },
    { 255 , 248 , 248 },
    { 255 , 248 , 251 },
    { 255 , 249 , 251 },
    { 255 , 249 , 253 },
    { 255 , 249 , 253 },
    { 254 , 249 , 255 },
    { 254 , 250 , 255 },
    { 252 , 247 , 255 },
    { 252 , 248 , 255 },
    { 249 , 246 , 255 },
    { 250 , 247 , 255 },
    { 247 , 245 , 255 },
    { 247 , 245 , 255 },
    { 245 , 243 , 255 },
    { 245 , 244 , 255 },
    { 243 , 242 , 255 },
    { 243 , 243 , 255 },
    { 240 , 241 , 255 },
    { 241 , 241 , 255 },
    { 239 , 240 , 255 },
    { 239 , 240 , 255 },
    { 237 , 239 , 255 },
    { 238 , 239 , 255 },
    { 235 , 238 , 255 },
    { 236 , 238 , 255 },
    { 233 , 237 , 255 },
    { 234 , 237 , 255 },
    { 231 , 236 , 255 },
    { 233 , 236 , 255 },
    { 230 , 235 , 255 },
    { 231 , 234 , 255 },
    { 228 , 234 , 255 },
    { 229 , 233 , 255 },
    { 227 , 233 , 255 },
    { 228 , 233 , 255 },
    { 225 , 232 , 255 },
    { 227 , 232 , 255 },
    { 224 , 231 , 255 },
    { 225 , 231 , 255 },
    { 222 , 230 , 255 },
    { 224 , 230 , 255 },
    { 221 , 230 , 255 },
    { 223 , 229 , 255 },
    { 220 , 229 , 255 },
    { 221 , 228 , 255 },
    { 218 , 228 , 255 },
    { 220 , 227 , 255 },
    { 217 , 227 , 255 },
    { 219 , 226 , 255 },
    { 216 , 227 , 255 },
    { 218 , 226 , 255 },
    { 215 , 226 , 255 },
    { 217 , 225 , 255 },
    { 214 , 225 , 255 },
    { 216 , 224 , 255 },
    { 212 , 225 , 255 },
    { 215 , 223 , 255 },
    { 211 , 224 , 255 },
    { 214 , 223 , 255 },
    { 210 , 223 , 255 },
    { 213 , 222 , 255 },
    { 209 , 223 , 255 },
    { 212 , 221 , 255 },
    { 208 , 222 , 255 },
    { 211 , 221 , 255 },
    { 207 , 221 , 255 },
    { 210 , 220 , 255 },
    { 207 , 221 , 255 },
    { 209 , 220 , 255 },
    { 206 , 220 , 255 },
    { 208 , 219 , 255 },
    { 205 , 220 , 255 },
    { 207 , 218 , 255 },
    { 204 , 219 , 255 },
    { 207 , 218 , 255 },
    { 203 , 219 , 255 },
    { 206 , 217 , 255 },
    { 202 , 218 , 255 },
    { 205 , 217 , 255 },
    { 201 , 218 , 255 },
    { 204 , 216 , 255 },
    { 201 , 217 , 255 },
    { 204 , 216 , 255 },
    { 200 , 217 , 255 },
    { 203 , 215 , 255 },
    { 199 , 216 , 255 },
    { 202 , 215 , 255 },
    { 199 , 216 , 255 },
    { 202 , 214 , 255 },
    { 198 , 216 , 255 },
    { 201 , 214 , 255 },
    { 197 , 215 , 255 },
    { 200 , 213 , 255 },
    { 196 , 215 , 255 },
    { 200 , 213 , 255 },
    { 196 , 214 , 255 },
    { 199 , 212 , 255 },
    { 195 , 214 , 255 },
    { 198 , 212 , 255 },
    { 195 , 214 , 255 },
    { 198 , 212 , 255 },
    { 194 , 213 , 255 },
    { 197 , 211 , 255 },
    { 193 , 213 , 255 },
    { 197 , 211 , 255 },
    { 193 , 212 , 255 },
    { 196 , 210 , 255 },
    { 192 , 212 , 255 },
    { 196 , 210 , 255 },
    { 192 , 212 , 255 },
    { 195 , 210 , 255 },
    { 191 , 211 , 255 },
    { 195 , 209 , 255 },
    { 191 , 211 , 255 },
    { 194 , 209 , 255 },
    { 190 , 211 , 255 },
    { 194 , 208 , 255 },
    { 190 , 210 , 255 },
    { 193 , 208 , 255 },
    { 189 , 210 , 255 },
    { 193 , 208 , 255 },
    { 189 , 210 , 255 },
    { 192 , 207 , 255 },
    { 188 , 210 , 255 },
    { 192 , 207 , 255 },
    { 188 , 209 , 255 },
    { 191 , 207 , 255 },
    { 187 , 209 , 255 },
    { 191 , 206 , 255 },
    { 187 , 209 , 255 },
    { 190 , 206 , 255 },
    { 186 , 208 , 255 },
    { 190 , 206 , 255 },
    { 186 , 208 , 255 },
    { 190 , 206 , 255 },
    { 185 , 208 , 255 },
    { 189 , 205 , 255 },
    { 185 , 208 , 255 },
    { 189 , 205 , 255 },
    { 185 , 207 , 255 },
    { 188 , 205 , 255 },
    { 184 , 207 , 255 },
    { 188 , 204 , 255 },
    { 184 , 207 , 255 },
    { 188 , 204 , 255 },
    { 183 , 207 , 255 },
    { 187 , 204 , 255 },
    { 183 , 206 , 255 },
    { 187 , 204 , 255 },
    { 183 , 206 , 255 },
    { 187 , 203 , 255 },
    { 182 , 206 , 255 },
    { 186 , 203 , 255 },
    { 182 , 206 , 255 },
    { 186 , 203 , 255 },
    { 182 , 205 , 255 },
    { 186 , 203 , 255 },
    { 181 , 205 , 255 },
    { 185 , 202 , 255 },
    { 181 , 205 , 255 },
    { 185 , 202 , 255 },
    { 181 , 205 , 255 },
    { 185 , 202 , 255 },
    { 180 , 205 , 255 },
    { 184 , 202 , 255 },
    { 180 , 204 , 255 },
    { 184 , 201 , 255 },
    { 180 , 204 , 255 },
    { 184 , 201 , 255 },
    { 179 , 204 , 255 },
    { 184 , 201 , 255 },
    { 179 , 204 , 255 },
    { 183 , 201 , 255 },
    { 179 , 204 , 255 },
    { 183 , 201 , 255 },
    { 178 , 203 , 255 },
    { 183 , 200 , 255 },
    { 178 , 203 , 255 },
    { 182 , 200 , 255 },
    { 178 , 203 , 255 },
    { 182 , 200 , 255 },
    { 178 , 203 , 255 },
    { 182 , 200 , 255 },
    { 177 , 203 , 255 },
    { 182 , 200 , 255 },
    { 177 , 202 , 255 },
    { 181 , 199 , 255 },
    { 177 , 202 , 255 },
    { 181 , 199 , 255 },
    { 177 , 202 , 255 },
    { 181 , 199 , 255 },
    { 176 , 202 , 255 },
    { 181 , 199 , 255 },
    { 176 , 202 , 255 },
    { 180 , 199 , 255 },
    { 176 , 202 , 255 },
    { 180 , 198 , 255 },
    { 175 , 201 , 255 },
    { 180 , 198 , 255 },
    { 175 , 201 , 255 },
    { 180 , 198 , 255 },
    { 175 , 201 , 255 },
    { 179 , 198 , 255 },
    { 175 , 201 , 255 },
    { 179 , 198 , 255 },
    { 175 , 201 , 255 },
    { 179 , 198 , 255 },
    { 174 , 201 , 255 },
    { 179 , 197 , 255 },
    { 174 , 201 , 255 },
    { 179 , 197 , 255 },
    { 174 , 200 , 255 },
    { 178 , 197 , 255 },
    { 174 , 200 , 255 },
    { 178 , 197 , 255 },
    { 173 , 200 , 255 },
    { 178 , 197 , 255 },
    { 173 , 200 , 255 },
    { 178 , 197 , 255 },
    { 173 , 200 , 255 },
    { 178 , 196 , 255 },
    { 173 , 200 , 255 },
    { 177 , 196 , 255 },
    { 173 , 200 , 255 },
    { 177 , 196 , 255 },
    { 172 , 199 , 255 },
    { 177 , 196 , 255 },
    { 172 , 199 , 255 },
    { 177 , 196 , 255 },
    { 172 , 199 , 255 },
    { 177 , 196 , 255 },
    { 172 , 199 , 255 },
    { 176 , 196 , 255 },
    { 172 , 199 , 255 },
    { 176 , 195 , 255 },
    { 171 , 199 , 255 },
    { 176 , 195 , 255 },
    { 171 , 199 , 255 },
    { 176 , 195 , 255 },
    { 171 , 199 , 255 },
    { 176 , 195 , 255 },
    { 171 , 198 , 255 },
    { 176 , 195 , 255 },
    { 171 , 198 , 255 },
    { 175 , 195 , 255 },
    { 170 , 198 , 255 },
    { 175 , 195 , 255 },
    { 170 , 198 , 255 },
    { 175 , 194 , 255 },
    { 170 , 198 , 255 },
    { 175 , 194 , 255 },
    { 170 , 198 , 255 },
    { 175 , 194 , 255 },
    { 170 , 198 , 255 },
    { 175 , 194 , 255 },
    { 170 , 198 , 255 },
    { 174 , 194 , 255 },
    { 169 , 198 , 255 },
    { 174 , 194 , 255 },
    { 169 , 197 , 255 },
    { 174 , 194 , 255 },
    { 169 , 197 , 255 },
    { 174 , 194 , 255 },
    { 169 , 197 , 255 },
    { 174 , 194 , 255 },
    { 169 , 197 , 255 },
    { 174 , 193 , 255 },
    { 169 , 197 , 255 },
    { 174 , 193 , 255 },
    { 169 , 197 , 255 },
    { 173 , 193 , 255 },
    { 168 , 197 , 255 },
    { 173 , 193 , 255 },
    { 168 , 197 , 255 },
    { 173 , 193 , 255 },
    { 168 , 197 , 255 },
    { 173 , 193 , 255 },
    { 168 , 197 , 255 },
    { 173 , 193 , 255 },
    { 168 , 196 , 255 },
    { 173 , 193 , 255 },
    { 168 , 196 , 255 },
    { 173 , 193 , 255 },
    { 168 , 196 , 255 },
    { 173 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 167 , 196 , 255 },
    { 172 , 192 , 255 },
    { 166 , 196 , 255 },
    { 172 , 192 , 255 },
    { 166 , 195 , 255 },
    { 171 , 192 , 255 },
    { 166 , 195 , 255 },
    { 171 , 192 , 255 },
    { 166 , 195 , 255 },
    { 171 , 191 , 255 },
    { 166 , 195 , 255 },
    { 171 , 191 , 255 },
    { 166 , 195 , 255 },
    { 171 , 191 , 255 },
    { 166 , 195 , 255 },
    { 171 , 191 , 255 },
    { 166 , 195 , 255 },
    { 171 , 191 , 255 },
    { 165 , 195 , 255 },
    { 171 , 191 , 255 },
    { 165 , 195 , 255 },
    { 171 , 191 , 255 },
    { 165 , 195 , 255 },
    { 170 , 191 , 255 },
    { 165 , 195 , 255 },
    { 170 , 191 , 255 },
    { 165 , 195 , 255 },
    { 170 , 191 , 255 },
    { 165 , 195 , 255 },
    { 170 , 191 , 255 },
    { 165 , 194 , 255 },
    { 170 , 190 , 255 },
    { 165 , 194 , 255 },
    { 170 , 190 , 255 },
    { 165 , 194 , 255 },
    { 170 , 190 , 255 },
    { 164 , 194 , 255 },
    { 170 , 190 , 255 },
    { 164 , 194 , 255 },
    { 170 , 190 , 255 },
    { 164 , 194 , 255 },
    { 170 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 164 , 194 , 255 },
    { 169 , 190 , 255 },
    { 163 , 194 , 255 },
    { 169 , 190 , 255 },
    { 163 , 194 , 255 },
    { 169 , 189 , 255 },
    { 163 , 193 , 255 },
    { 169 , 189 , 255 },
    { 163 , 193 , 255 },
    { 169 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 163 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 168 , 189 , 255 },
    { 162 , 193 , 255 },
    { 167 , 188 , 255 },
    { 162 , 193 , 255 },
    { 167 , 188 , 255 },
    { 162 , 193 , 255 },
    { 167 , 188 , 255 },
    { 162 , 192 , 255 },
    { 167 , 188 , 255 },
    { 162 , 192 , 255 },
    { 167 , 188 , 255 },
    { 162 , 192 , 255 },
    { 167 , 188 , 255 },
    { 162 , 192 , 255 },
    { 167 , 188 , 255 },
    { 162 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 167 , 188 , 255 },
    { 161 , 192 , 255 },
    { 166 , 188 , 255 },
    { 161 , 192 , 255 },
    { 166 , 188 , 255 },
    { 161 , 192 , 255 },
    { 166 , 188 , 255 },
    { 161 , 192 , 255 },
    { 166 , 187 , 255 },
    { 161 , 192 , 255 },
    { 166 , 187 , 255 },
    { 161 , 192 , 255 },
    { 166 , 187 , 255 },
    { 161 , 192 , 255 },
    { 166 , 187 , 255 },
    { 161 , 192 , 255 },
    { 166 , 187 , 255 },
    { 160 , 192 , 255 },
    { 166 , 187 , 255 },
    { 160 , 192 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 166 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 160 , 191 , 255 },
    { 165 , 187 , 255 },
    { 159 , 191 , 255 },
    { 165 , 187 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 191 , 255 },
    { 165 , 186 , 255 },
    { 159 , 190 , 255 },
    { 165 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 159 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 186 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 164 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 158 , 190 , 255 },
    { 163 , 185 , 255 },
    { 157 , 190 , 255 },
    { 163 , 185 , 255 },
    { 157 , 190 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 185 , 255 },
    { 157 , 189 , 255 },
    { 163 , 184 , 255 },
    { 157 , 189 , 255 },
    { 163 , 184 , 255 },
    { 157 , 189 , 255 },
    { 162 , 184 , 255 },
    { 157 , 189 , 255 },
    { 162 , 184 , 255 },
    { 157 , 189 , 255 },
    { 162 , 184 , 255 },
    { 157 , 189 , 255 },
    { 162 , 184 , 255 },
    { 157 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 189 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 162 , 184 , 255 },
    { 156 , 188 , 255 },
    { 161 , 184 , 255 },
    { 156 , 188 , 255 },
    { 161 , 184 , 255 },
    { 155 , 188 , 255 },
    { 161 , 184 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 },
    { 161 , 183 , 255 },
    { 155 , 188 , 255 }
};

void colortemp_apply( void *ptr, VJFrame *frame, int *args ) {
    const int temperature = args[0];
	const int mode = args[1];
	int opacity = args[2];

	int i;
	uint8_t *Y = frame->data[0];	
	uint8_t *U = frame->data[1];
	uint8_t *V = frame->data[2];

    int iy = pixel_Y_lo_;
    int iu = 128;
    int iv = 128;

    _rgb2yuv( blackbody_t[temperature].r,
			  blackbody_t[temperature].g,
			  blackbody_t[temperature].b,
			  iy,iu,iv );

	iu -= 128;
	iv -= 128;

	if( mode == 1 ) {
		uint64_t sum = 0;
#pragma omp simd
		for( i = 0; i < frame->len; i ++ ) {
			sum += Y[i];
		}
		opacity = sum / frame->len;
	}

#pragma omp simd
    for ( i = 0; i < frame->len; i++) {
		int u = U[i] - 128;
		int v = V[i] - 128;

   		u = 128 + ((opacity * (u - iu) >> 8 ) + u);
   		v = 128 + ((opacity * (v - iv) >> 8 ) + v);
	
	    u = (u < 0) ? 0 : (u > 255) ? 255 : u;
	    v = (v < 0) ? 0 : (v > 255) ? 255 : v;

		U[i] = (uint8_t) u;
		V[i] = (uint8_t) v;
	}

}

