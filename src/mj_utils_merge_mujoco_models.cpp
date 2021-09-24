#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

#include "pugixml/pugixml.hpp"

#include <mc_rtc/logging.h>

namespace mc_mujoco
{

static pugi::xml_node get_child_or_create(pugi::xml_node & out, const char * name)
{
  auto child = out.child(name);
  if(!child)
  {
    child = out.append_child(name);
  }
  return child;
}

static void merge_mujoco_size(const pugi::xml_node & in, pugi::xml_node & out)
{
  static const char * attributes[] = {"njmax",        "nconmax",        "nstack",      "nuserdata",  "nkey",
                                      "nuser_body",   "nuser_jnt",      "nuser_geom",  "nuser_site", "nuser_cam",
                                      "nuser_tendon", "nuser_actuator", "nuser_sensor"};
  for(const auto & attr : attributes)
  {
    auto in_attr = in.attribute(attr);
    if(!in_attr)
    {
      continue;
    }
    auto out_attr = out.attribute(attr);
    if(out_attr)
    {
      out_attr.set_value(out_attr.as_int() + in_attr.as_int());
    }
    else
    {
      out.append_attribute(attr).set_value(in_attr.value());
    }
  }
}

static void merge_mujoco_node(const std::string & node,
                              const std::string & fileIn,
                              const pugi::xml_node & in,
                              pugi::xml_node & out,
                              const std::vector<std::string> & exclude = {})
{
  for(const auto & attr : in.attributes())
  {
    if(std::find(exclude.begin(), exclude.end(), attr.name()) != exclude.end())
    {
      continue;
    }
    auto out_attr = out.attribute(attr.name());
    if(!out_attr)
    {
      out.append_attribute(attr.name()).set_value(attr.value());
      continue;
    }
    if(strcmp(out_attr.value(), attr.value()) != 0)
    {
      mc_rtc::log::critical("[mc_mujoco] Different mujoco attributes when merging models, the first loaded value will "
                            "prevail (in {} node, attribute {}, value in {}: {}, value in merged model: {})",
                            node, attr.name(), fileIn, attr.value(), out_attr.value());
    }
  }
}

static void merge_mujoco_compiler(const std::string & fileIn, const pugi::xml_node & in, pugi::xml_node & out)
{
  merge_mujoco_node("compiler", fileIn, in, out, {"meshdir", "texturedir"});
}

static void merge_mujoco_option(const std::string & fileIn, const pugi::xml_node & in, pugi::xml_node & out)
{
  merge_mujoco_node("option", fileIn, in, out);
  const auto & flag = in.child("flag");
  if(flag)
  {
    auto flag_out = get_child_or_create(out, "flag");
    merge_mujoco_node("option/flag", fileIn, flag, flag_out);
  }
}

static void add_prefix(const std::string & prefix, pugi::xml_node & n, const char * attr)
{
  auto n_attr = n.attribute(attr);
  if(n_attr)
  {
    n_attr.set_value(fmt::format("{}_{}", prefix, n_attr.value()).c_str());
  }
}

static void add_prefix_recursively(const std::string & prefix,
                                   pugi::xml_node & out,
                                   const std::vector<std::string> & attrs)
{
  for(const auto & attr : attrs)
  {
    add_prefix(prefix, out, attr.c_str());
  }
  for(auto & c : out.children())
  {
    add_prefix_recursively(prefix, c, attrs);
  }
}

static void merge_mujoco_default(const std::string & fileIn,
                                 const pugi::xml_node & in,
                                 pugi::xml_node & out,
                                 const std::string & robot)
{
  for(const auto & c : in.children())
  {
    if(strcmp(c.name(), "default") == 0)
    {
      auto c_out = out.append_copy(c);
      add_prefix_recursively(robot, c_out, {"class", "material", "hfield", "mesh", "target"});
    }
    else
    {
      auto c_out = get_child_or_create(out, c.name());
      merge_mujoco_node(fmt::format("default/{}", c.name()), fileIn, c, c_out);
    }
  }
}

static void merge_mujoco_visual(const std::string & fileIn, const pugi::xml_node & in, pugi::xml_node & out)
{
  for(const auto & c : in.children())
  {
    auto c_out = get_child_or_create(out, c.name());
    merge_mujoco_node(fmt::format("visual/{}", c.name()), fileIn, c, c_out);
  }
}

static void copy_and_add_prefix(const pugi::xml_node & in,
                                pugi::xml_node & out,
                                const char * name,
                                const std::string & prefix,
                                const std::vector<std::string> & attrs)
{
  for(const auto & c : in.children(name))
  {
    auto c_out = out.append_copy(c);
    for(const auto & attr : attrs)
    {
      add_prefix(prefix, c_out, attr.c_str());
    }
  }
}

static void merge_mujoco_asset(const pugi::xml_node & in,
                               pugi::xml_node & out,
                               const bfs::path & meshPath,
                               const bfs::path & texturePath,
                               const std::string & robot)
{
  auto update_name = [&](pugi::xml_node & n) { add_prefix(robot, n, "name"); };
  auto update_file = [&](pugi::xml_node & n, const bfs::path & dir) {
    auto n_file = n.attribute("file");
    if(n_file)
    {
      bfs::path n_path(n_file.value());
      if(!n_path.is_absolute())
      {
        n_file.set_value(bfs::absolute(dir / n_path).c_str());
      }
    }
  };
  for(const auto & hf : in.children("hfield"))
  {
    auto hf_out = out.append_copy(hf);
    update_name(hf_out);
  }
  auto copy_assets = [&](const char * type, const bfs::path & dir) {
    for(const auto & n : in.children(type))
    {
      auto n_out = out.append_copy(n);
      update_name(n_out);
      update_file(n_out, dir);
    }
  };
  for(const auto & s : in.children("skin"))
  {
    auto s_out = out.append_copy(s);
    update_name(s_out);
    update_file(s_out, meshPath);
    for(auto & bone : s_out.children("bone"))
    {
      add_prefix(robot, bone, "body");
    }
  }
  for(const auto & mat : in.children("material"))
  {
    auto mat_out = out.append_copy(mat);
    update_name(mat_out);
    add_prefix(robot, mat_out, "texture");
  }
  copy_assets("texture", texturePath);
  copy_assets("mesh", meshPath);
}

static void merge_mujoco_contact(const pugi::xml_node & in, pugi::xml_node & out, const std::string & robot)
{
  copy_and_add_prefix(in, out, "pair", robot, {"name", "class", "geom1", "geom2"});
  copy_and_add_prefix(in, out, "exclude", robot, {"name", "body1", "body2"});
}

static void merge_mujoco_actuator(const pugi::xml_node & in, pugi::xml_node & out, const std::string & robot)
{
  for(const auto & c : in.children())
  {
    auto c_out = out.append_copy(c);
    for(const auto & attr : {"name", "class", "joint", "jointinparent", "site", "tendon", "cranksite", "slidersite"})
    {
      add_prefix(robot, c_out, attr);
    }
  }
}

static void merge_mujoco_sensor(const pugi::xml_node & in, pugi::xml_node & out, const std::string & robot)
{
  for(const auto & c : in.children())
  {
    auto c_out = out.append_copy(c);
    for(const auto & attr : {"name", "site", "joint", "actuator", "tendon", "objname", "body"})
    {
      add_prefix(robot, c_out, attr);
    }
  }
}

static void merge_mujoco_worldbody(const pugi::xml_node & in, pugi::xml_node & out, const std::string & robot)
{
  for(const auto & c : in.children())
  {
    auto out_c = out.append_copy(c);
    add_prefix_recursively(robot, out_c, {"name", "childclasss", "class", "material", "hfield", "mesh", "target"});
  }
}

static bfs::path get_mujoco_path(const std::string & xmlFile, const pugi::xml_node & in, const char * attr)
{
  bfs::path xmlPath = bfs::path(xmlFile).parent_path();
  auto dirAttr = in.child("compiler").attribute(attr);
  if(!dirAttr)
  {
    return xmlPath;
  }
  bfs::path dir = bfs::path(dirAttr.value());
  if(dir.is_absolute())
  {
    return dir;
  }
  else
  {
    return bfs::absolute(xmlPath / dir);
  }
}

static void merge_mujoco_model(const std::string & robot, const std::string & xmlFile, pugi::xml_node & out)
{
  pugi::xml_document in;
  if(!in.load_file(xmlFile.c_str()))
  {
    mc_rtc::log::error_and_throw<std::runtime_error>("Failed to load {}", xmlFile);
  }
  auto root = in.child("mujoco");
  if(!root)
  {
    mc_rtc::log::error_and_throw<std::runtime_error>("No mujoco root node in {}", xmlFile);
  }
  /** Merge compiler flags */
  {
    auto compiler_out = get_child_or_create(out, "compiler");
    merge_mujoco_compiler(xmlFile, root.child("compiler"), compiler_out);
  }
  /** Merge size attributes */
  {
    auto size_out = get_child_or_create(out, "size");
    merge_mujoco_size(root.child("size"), size_out);
  }
  /** Merge option flags */
  {
    auto option_out = get_child_or_create(out, "option");
    merge_mujoco_option(xmlFile, root.child("option"), option_out);
  }
  /** Merge defaults */
  {
    auto default_out = get_child_or_create(out, "default");
    merge_mujoco_default(xmlFile, root.child("default"), default_out, robot);
  }
  /** Merge visual */
  {
    auto visual_out = get_child_or_create(out, "visual");
    merge_mujoco_visual(xmlFile, root.child("visual"), visual_out);
  }
  /** Merge asset */
  {
    auto meshPath = get_mujoco_path(xmlFile, root, "meshdir");
    auto texturePath = get_mujoco_path(xmlFile, root, "texturedir");
    auto asset_out = get_child_or_create(out, "asset");
    merge_mujoco_asset(root.child("asset"), asset_out, meshPath, texturePath, robot);
  }
  /** Merge contact */
  {
    auto contact_out = get_child_or_create(out, "contact");
    merge_mujoco_contact(root.child("contact"), contact_out, robot);
  }
  /** Merge actuator */
  {
    auto actuator_out = get_child_or_create(out, "actuator");
    merge_mujoco_actuator(root.child("actuator"), actuator_out, robot);
  }
  /** Merge sensor */
  {
    auto sensor_out = get_child_or_create(out, "sensor");
    merge_mujoco_sensor(root.child("sensor"), sensor_out, robot);
  }
  /** Merge worldbody */
  {
    auto worldbody_out = get_child_or_create(out, "worldbody");
    merge_mujoco_worldbody(root.child("worldbody"), worldbody_out, robot);
  }
  // FIXME Not handled but easy to do based on contact: equality/tendon/keyframe
}

std::string merge_mujoco_models(const std::vector<std::string> & robots, const std::vector<std::string> & xmlFiles)
{
  if(xmlFiles.size() == 1)
  {
    return xmlFiles[0];
  }
  std::string outFile = "/tmp/mc_mujoco.xml";
  pugi::xml_document out_doc;
  auto out = out_doc.append_child("mujoco");
  out.append_attribute("model").set_value("mc_mujoco");
  for(size_t i = 0; i < robots.size(); ++i)
  {
    merge_mujoco_model(robots[i], xmlFiles[i], out);
  }
  {
    std::ofstream ofs(outFile);
    out_doc.save(ofs, "    ");
  }
  return outFile;
}

} // namespace mc_mujoco
