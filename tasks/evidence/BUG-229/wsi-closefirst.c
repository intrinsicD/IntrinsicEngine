// Diagnostic Vulkan/XCB lifecycle probe, independent of IntrinsicEngine and GLFW.
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#call,r);abort();}}while(0)
int main(int argc,char**argv) {
 int stage=argc>1?atoi(argv[1]):3, repeats=argc>2?atoi(argv[2]):1;
 xcb_connection_t*c=xcb_connect(0,0);if(xcb_connection_has_error(c))return 2;
 xcb_screen_t*s=xcb_setup_roots_iterator(xcb_get_setup(c)).data;
 xcb_window_t w=xcb_generate_id(c);
 xcb_create_window(c,XCB_COPY_FROM_PARENT,w,s->root,0,0,128,128,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,s->root_visual,0,0);
 xcb_map_window(c,w);xcb_flush(c);
 const char*extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XCB_SURFACE_EXTENSION_NAME};
 VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
 VkInstance instance;CHECK(vkCreateInstance(&ii,0,&instance));
 uint32_t n=8;VkPhysicalDevice physical[8];CHECK(vkEnumeratePhysicalDevices(instance,&n,physical));if(!n)return 3;
 VkSurfaceKHR surface=0;VkDevice device=0;VkSwapchainKHR swap=0;
 if(stage>=1){VkXcbSurfaceCreateInfoKHR si={.sType=VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,.connection=c,.window=w};CHECK(vkCreateXcbSurfaceKHR(instance,&si,0,&surface));}
 if(stage>=2){for(int i=0;i<repeats;i++) {VkSurfaceCapabilitiesKHR caps;CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical[0],surface,&caps));}}
 if(stage>=3){
 uint32_t qn=32;VkQueueFamilyProperties qp[32];vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&qn,qp);uint32_t q=0;
 for(;q<qn;q++){VkBool32 present=0;CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical[0],q,surface,&present));if(present&&(qp[q].queueFlags&VK_QUEUE_GRAPHICS_BIT))break;}if(q==qn)return 4;
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=q,.queueCount=1,.pQueuePriorities=&priority};const char*de=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=&de};CHECK(vkCreateDevice(physical[0],&di,0,&device));
 VkSurfaceCapabilitiesKHR caps;CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical[0],surface,&caps));uint32_t fn=32;VkSurfaceFormatKHR formats[32];CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical[0],surface,&fn,formats));
 VkSwapchainCreateInfoKHR sc={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,.minImageCount=caps.minImageCount,.imageFormat=formats[0].format,.imageColorSpace=formats[0].colorSpace,.imageExtent=caps.currentExtent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=caps.currentTransform,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
 CHECK(vkCreateSwapchainKHR(device,&sc,0,&swap));CHECK(vkDeviceWaitIdle(device));vkDestroySwapchainKHR(device,swap,0);vkDestroyDevice(device,0);
 }
 if(surface)vkDestroySurfaceKHR(instance,surface,0);
 xcb_destroy_window(c,w);xcb_flush(c);xcb_disconnect(c);
 vkDestroyInstance(instance,0);
 fprintf(stderr,"WSI_PROBE_COMPLETED stage=%d repeats=%d\n",stage,repeats);
}
