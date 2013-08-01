//#include <string.h>
//#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TTree.h"
#include "TKey.h"
//#include "Riostream.h"

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <string>
#include <vector>
#include "getopt.h"

void hadd(std::string& targetName, std::vector<std::string>& sources);
void MergeRootfile(std::map<std::pair<std::string, std::string>, TObject*>& output, TDirectory *target, TFile *source);

std::vector<std::pair<std::string, TFile*> > tmpFiles;

void hadd(std::string& targetName, std::vector<std::string>& sources) 
{
    using namespace std;
    TFile* target = TFile::Open("result.root", "RECREATE");    
 
    map<pair<string, string>, TObject*> output;

    for(vector<string>::const_iterator iF = sources.begin(); iF != sources.end(); ++iF) 
    {
	TFile * f = new TFile(iF->c_str());
	MergeRootfile(output, f, f);
	f->Close();
    }

    target->cd();
    map<string, TDirectory*> paths;
    for(map<pair<string, string>, TObject*>::const_iterator iO = output.begin(); iO != output.end(); ++iO)
    {
	if(iO->second == 0 && iO->first.first.size() > 0)
	{
	    if(paths.find(iO->first.first) == paths.end())
	    {
		size_t pos = iO->first.first.rfind('/');
		if(pos == size_t(-1)) 
		{
		    target->cd();
		    pos = 0;
		}
		else 
		{
		    target->cd(iO->first.first.substr(0, pos).c_str());
		    pos++;
		}
		paths[iO->first.first] = gDirectory->mkdir(iO->first.first.substr(pos).c_str());
	    }            
	}
	else 
	{
	    paths[iO->first.first]->cd();
	    if(iO->second->IsA()->InheritsFrom( TH1::Class() )) iO->second->Write();
	    else iO->second->Write();;
	}
    }

    target->Close();
    for(vector<pair<string, TFile*> >::const_iterator iF = tmpFiles.begin(); iF != tmpFiles.end(); ++iF)
    {
	if(iF->second) 
	{
	    iF->second->Close();
	    system(("rm " + iF->first).c_str());
	}

    }
}

void MergeRootfile(std::map<std::pair<std::string, std::string>, TObject*>& output, TDirectory *target, TFile* source) 
{
    using namespace std;
    TString path( (char*)strstr( target->GetPath(), ":" ) );
    path.Remove( 0, 2 );
  
    source->cd( path );
    TDirectory *current_sourcedir = gDirectory;
    //gain time, do not add the objects in the list in memory
    Bool_t status = TH1::AddDirectoryStatus();
    TH1::AddDirectory(kFALSE);
  
    // loop over all keys in this directory
    TIter nextkey( current_sourcedir->GetListOfKeys() );
    TKey *key, *oldkey=0;
    while(key = (TKey*)nextkey()) 
    {    
	//keep only the highest cycle number for each key
	if (oldkey && !strcmp(oldkey->GetName(),key->GetName())) continue;
	oldkey = key;

	// read object from first source file
	source->cd( path );
	TObject *obj = key->ReadObj();
    
	if(obj->IsA()->InheritsFrom(TH1::Class())) 
	{
	    //cout << "Histogram " << obj->GetName() << endl;
	    TH1 *h1 = (TH1*)obj;
	    string path(target->GetPath());
	    pair<string, string> okey(path.substr(path.find(':') + 2), obj->GetName());
	    //cout << okey.first << "\t" << okey.second << endl;
	    if(output.find(okey) == output.end()) output[okey] = (TObject*)obj;
	    else ((TH1*)output[okey])->Add((TH1*)obj);
	}
	else if(obj->IsA()->InheritsFrom(TTree::Class())) 
	{
	    //cout << "Histogram " << obj->GetName() << endl;
	    TH1 *h1 = (TH1*)obj;
            string path(target->GetPath());
            pair<string, string> okey(path.substr(path.find(':') + 2), obj->GetName());
	    //It seems what needs to be done is to CloneTree the first tree, then CopyEntries the rest.  Do not use TChains
            //cout << okey.first << "\t" << okey.second << endl;
            if(output.find(okey) == output.end())
	    {
		string fname(okey.first);
		fname = fname + "_" + obj->GetName() + "tmpfile";
		for(size_t pos; (pos = fname.find('/')) != size_t(-1);) fname[pos] = '_';
		tmpFiles.push_back(make_pair(fname, new TFile(fname.c_str(), "RECREATE")));
		TTree* tree = ((TTree*)obj)->CloneTree();
		output[okey] = (TObject*)tree;
	    }
            else ((TTree*)output[okey])->CopyEntries((TTree*)obj);
	}
	else if(obj->IsA()->InheritsFrom(TDirectory::Class())) 
	{
	    //cout << "Found subdirectory " << obj->GetName() << "\t" << ((TDirectory*)obj)->GetPath() << endl;
	    string path(((TDirectory*)obj)->GetPath());
            pair<string, string> okey(path.substr(path.find(':') + 2), " -------- ");
	    if(output.find(okey) == output.end()) output[okey] = 0;
	
      	    MergeRootfile(output, (TDirectory*)obj, source);
	}
	else
	{
	    cout << "Unknown object type, name: "
		 << obj->GetName() << " title: " << obj->GetTitle() << endl;
	}    
    } // while ( ( TKey *key = (TKey*)nextkey() ) )
}

int main(int argn, char * argv[])
{
    using namespace std;

    //get command line options 
    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"readEeproms",       no_argument, 0, 'r'},
        {"margUpTime",  required_argument, 0, 'U'},
    };

    while((opt = getopt_long(argn, argv, "rU:", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
	case 'r':
	    break;
	}
    }

    vector<string> sources;
    
    if(argn <= 1) 
    {
	printf("No target specified\n");
	exit(0);
    }
    string target(argv[1]);

    if(argn <= 2)
    {
	printf("No sources specified\n");
	exit(0);
    }
    for(int i = 2; i < argn; i++)
    {
	sources.push_back(argv[i]);
    }

    hadd(target, sources);
}
