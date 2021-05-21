RWByteAddressBuffer IndirectArg : register(u3);

[numthreads(4, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint value = (DTid.x == 0 || DTid.x == 3) ? 0 : 1;
	IndirectArg.Store(DTid.x * 4, value);
}