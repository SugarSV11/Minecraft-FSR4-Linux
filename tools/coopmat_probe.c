#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char*T(int t){switch(t){case 0:return"f16";case 1:return"f32";case 2:return"f64";case 3:return"s8";case 4:return"s16";case 5:return"s32";case 6:return"s64";case 7:return"u8";case 8:return"u16";case 9:return"u32";case 10:return"u64";case 1000141000:return"bf16";case 1000491000:return"sint8pk";case 1000491002:return"bf8?";case 1000567000:return"e4m3";case 1000567001:return"e5m2";}static char b[32];sprintf(b,"%d",t);return b;}
int main(){VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"cm",1,"cm",1,VK_API_VERSION_1_3};
VkInstanceCreateInfo ic={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,0,&ai};VkInstance i;vkCreateInstance(&ic,0,&i);
uint32_t n=0;vkEnumeratePhysicalDevices(i,&n,0);VkPhysicalDevice d[8];vkEnumeratePhysicalDevices(i,&n,d);
for(uint32_t k=0;k<n;k++){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(d[k],&p);if(!strstr(p.deviceName,"AMD"))continue;printf("%s\n",p.deviceName);
PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR f=(void*)vkGetInstanceProcAddr(i,"vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
uint32_t c=0;f(d[k],&c,0);VkCooperativeMatrixPropertiesKHR*m=calloc(c,sizeof*m);for(uint32_t j=0;j<c;j++)m[j].sType=VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;f(d[k],&c,m);
for(uint32_t j=0;j<c;j++)printf("M%u N%u K%u A=%s B=%s C=%s R=%s scope=%d sat=%d\n",m[j].MSize,m[j].NSize,m[j].KSize,T(m[j].AType),T(m[j].BType),T(m[j].CType),T(m[j].ResultType),m[j].scope,m[j].saturatingAccumulation);}
}
