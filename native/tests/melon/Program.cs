using AsmResolver.DotNet;
using AsmResolver.PE.DotNet.Metadata.Tables;
using EpochPact.MelonBridge;
using System.Security.Cryptography;

// Test copies only. The input is a backed-up generated interop cache, never a
// game assembly. Preserve the fixture folder to make failed cases inspectable.
string root=Path.Combine(Path.GetTempPath(),"EpochPact-interop-test-"+Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);string backup=Path.Combine(root,"backups");
string input=Path.GetFullPath(args[0]);int total=0,passed=0;
void Check(bool ok,string name){total++;if(ok)passed++;Console.WriteLine((ok?"PASS ":"FAIL ")+name);}
string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
string Copy(string name){string p=Path.Combine(root,name+".dll");File.Copy(input,p);return p;}
bool Refuses(string p){try{InteropCacheRepair.Repair(p,backup);return false;}catch(IOException){return true;}}
string[] Attributes(AssemblyDefinition a)=>a.Modules.SelectMany(m=>m.GetAllTypes()).SelectMany(t=>t.CustomAttributes).Select(c=>c.Constructor?.FullName+"|"+string.Join(";",c.Signature?.FixedArguments.Select(v=>v.ToString())??Array.Empty<string>())).ToArray();
string p=Copy("duplicates"),original=Hash(p);var before=AssemblyDefinition.FromFile(p);
int count=InteropCacheRepair.Repair(p,backup);var after=AssemblyDefinition.FromFile(p);
Check(count==2&&!after.Modules.SelectMany(m=>m.GetAllTypes()).GroupBy(t=>t.FullName).Any(g=>g.Count()>1),"actual generated duplicate types repaired");
Check(Hash(Path.Combine(backup,Path.GetFileName(p)+"."+original+".bak"))==original,"original bytes backed up exactly");
Check(Attributes(before).SequenceEqual(Attributes(after)),"native-name custom attributes preserved");
string repaired=Hash(p);Check(InteropCacheRepair.Repair(p,backup)==0&&Hash(p)==repaired,"second run leaves valid cache byte-identical");
string foreign=Copy("foreign");var a=AssemblyDefinition.FromFile(foreign);var m=a.ManifestModule!;
m.TopLevelTypes.Add(new TypeDefinition("Fixture","RealClass",TypeAttributes.Public,m.CorLibTypeFactory.Object.Type));
m.TopLevelTypes.Add(new TypeDefinition("Fixture","RealClass",TypeAttributes.Public,m.CorLibTypeFactory.Object.Type));a.Write(foreign);
string foreignHash=Hash(foreign);Check(Refuses(foreign)&&Hash(foreign)==foreignHash,"unexpected duplicates keep original");
string collision=Copy("collision");a=AssemblyDefinition.FromFile(collision);
var duplicate=a.Modules.SelectMany(mod=>mod.GetAllTypes()).GroupBy(t=>t.FullName).First(g=>g.Count()>1).First();
var added=new TypeDefinition(duplicate.Namespace,"<>O__EpochPactInterop_1",duplicate.Attributes,a.ManifestModule!.CorLibTypeFactory.Object.Type);
if(duplicate.DeclaringType!=null)duplicate.DeclaringType.NestedTypes.Add(added);else a.ManifestModule!.TopLevelTypes.Add(added);a.Write(collision);
string collisionHash=Hash(collision);Check(Refuses(collision)&&Hash(collision)==collisionHash,"renaming collision fails without publishing");
string badBackup=Copy("bad-backup"),badHash=Hash(badBackup);File.WriteAllText(Path.Combine(backup,Path.GetFileName(badBackup)+"."+badHash+".bak"),"foreign backup");
Check(Refuses(badBackup)&&Hash(badBackup)==badHash,"existing invalid backup prevents repair");
string corrupt=Path.Combine(root,"corrupt.dll");File.WriteAllText(corrupt,"invalid assembly");string corruptHash=Hash(corrupt);
bool rejected=false;try{InteropCacheRepair.Repair(corrupt,backup);}catch{rejected=true;}
Check(rejected&&Hash(corrupt)==corruptHash,"unreadable cache keeps original");
Console.WriteLine($"InteropCacheTest: {passed}/{total} passed; fixtures: {root}");return total-passed;
