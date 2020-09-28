#include <simulation_controller/color_utils/color_utils.hpp>

#include <iostream>

namespace color_utils
{
/**
 * @brief generate std_msgs::msg::ColorRGBA message from hsv values
 * @param h hue 
 * @param s saturation 
 * @param v value
 * @param alpha alpha value of the color
 * @return std_msgs::msg::ColorRGBA 
 */
std_msgs::ColorRGBA fromHsv(double h, double s, double v, double alpha)
{
  std_msgs::ColorRGBA color;
  color.a = alpha;
  float r = v;
  float g = v;
  float b = v;
  if (s > 0.0f) {
    h *= 6.0f;
    int i = (int)h;
    float f = h - (float)i;
    switch (i) {
      default:
      case 0:
        g *= 1 - s * (1 - f);
        b *= 1 - s;
        break;
      case 1:
        r *= 1 - s * f;
        b *= 1 - s;
        break;
      case 2:
        r *= 1 - s;
        b *= 1 - s * (1 - f);
        break;
      case 3:
        r *= 1 - s;
        g *= 1 - s * f;
        break;
      case 4:
        r *= 1 - s * (1 - f);
        g *= 1 - s;
        break;
      case 5:
        g *= 1 - s;
        b *= 1 - s * f;
        break;
    }
  }
  color.r = r;
  color.g = g;
  color.b = b;
  return color;
}

/**
 * @brief generate std_msgs::msg::ColorRGBA message from color name
 * @param preset_name the name of the color
 * @param alpha alpha value of the color
 * @return std_msgs::msg::ColorRGBA 
 */
std_msgs::ColorRGBA makeColorMsg(std::string preset_name, double alpha)
{
  std_msgs::ColorRGBA c_msg;
  c_msg.a = alpha;
  if (c_msg.a < 0.) c_msg.a = 0.;
  if (c_msg.a > 1.) c_msg.a = 1.;

  auto found_itr = COLOR_NAME_DICT.find(preset_name);
  if (found_itr != COLOR_NAME_DICT.end()) {
    c_msg.r = found_itr->second[0];
    c_msg.g = found_itr->second[1];
    c_msg.b = found_itr->second[2];
  } else {
    c_msg.r = 0;
    c_msg.g = 0;
    c_msg.b = 0;
  }
  return c_msg;
}
}  // namespace color_util