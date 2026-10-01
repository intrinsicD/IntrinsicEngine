#include <vulkan/vulkan.h>
#include <stdio.h>
int main(void) {
 VkInstance instance;
 VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
 VkResult r=vkCreateInstance(&info,0,&instance);
 if(r!=VK_SUCCESS)return 2;
 uint32_t count=0;
 r=vkEnumeratePhysicalDevices(instance,&count,0);
 printf("enumeration=%d devices=%u\n",r,count);
 vkDestroyInstance(instance,0);
 return r!=VK_SUCCESS;
}
