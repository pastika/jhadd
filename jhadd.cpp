#include "TFile.h"
#include "TH1.h"
#include "TTree.h"
#include "TKey.h"
#include "TBranch.h"
#include "TVirtualIndex.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <string>
#include <vector>
#include <algorithm>
#include "getopt.h"

void hadd(std::string& targetName, std::vector<std::string>& sources);
void MergeRootfile(std::map<std::pair<std::string, std::string>, TObject*>& outputMap, std::vector<std::pair<std::string, TObject*> >& outputVec, TDirectory *target, TFile *source);

// list of temporary files where TTrees are stored
std::vector<std::pair<std::string, TFile*> > tmpFiles;

// option parameters
bool isForce = false;
int verbosity = 2;

void hadd(std::string& targetName, std::vector<std::string>& sources)
{
    using namespace std;
    TFile* target = 0;
    if(isForce) target = TFile::Open(targetName.c_str(), "RECREATE");
    else        target = TFile::Open(targetName.c_str(),   "CREATE");
    if(!target)
    {
        printf("target file already exists! (use -f to force recreation)\n");
        exit(0);
    }

    map<pair<string, string>, TObject*> outputMap;
    vector<pair<string, TObject*> > outputVec;

    for(vector<string>::const_iterator iF = sources.begin(); iF != sources.end(); ++iF)
    {
        if(verbosity >= 2) 
        {
            printf("Processing source file: %s\n", iF->c_str());
            fflush(stdout);
        }
        TFile * f = new TFile(iF->c_str());
        MergeRootfile(outputMap, outputVec, f, f);
        f->Close();
    }

    target->cd();
    map<string, TDirectory*> paths;
    for(std::vector<std::pair<std::string, TObject*> >::const_iterator iO = outputVec.begin(); iO != outputVec.end(); ++iO)
    {
        if(iO->second == 0)
        {
            if(paths.find(iO->first) == paths.end())
            {
                size_t pos = iO->first.rfind('/');
                if(pos == size_t(-1))
                {
                    target->cd();
                    pos = 0;
                }
                else
                {
                    target->cd(iO->first.substr(0, pos).c_str());
                    pos++;
                }
                paths[iO->first] = gDirectory->mkdir(iO->first.substr(pos).c_str());
            }
        }
        else
        {
            paths[iO->first]->cd();
            if(iO->second->IsA()->InheritsFrom(TH1::Class())) iO->second->Write();
            else
            {
                if(iO->second && ((TTree*)iO->second)->GetTreeIndex()) ((TTree*)iO->second)->GetTreeIndex()->Append(0, kFALSE); // Force the sorting
                TTree* tree = ((TTree*)iO->second)->CloneTree();
                ((TTree*)iO->second)->GetListOfClones()->Remove(tree);
                ((TTree*)iO->second)->ResetBranchAddresses();
                tree->ResetBranchAddresses();
                tree->Write();
            }
        }
    }
    
    if(verbosity >= 3)
    {
        printf("Results written to target file: %s\n", target->GetName());
        fflush(stdout);
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

void MergeRootfile(std::map<std::pair<std::string, std::string>, TObject*>& outputMap, std::vector<std::pair<std::string, TObject*> >& outputVec, TDirectory *target, TFile *source)
{
    using namespace std;
    string path(target->GetPath());
    path = path.substr(path.find(":") + 1);

    source->cd(path.c_str());
    TDirectory *current_sourcedir = gDirectory;
    //gain time, do not add the objects in the list in memory
    Bool_t status = TH1::AddDirectoryStatus();
    TH1::AddDirectory(kFALSE);

    // loop over all keys in this directory
    TIter nextkey( current_sourcedir->GetListOfKeys() );
    TKey *key, *oldkey = 0;
    while(key = (TKey*)nextkey())
    {
        //keep only the highest cycle number for each key
        if (oldkey && !strcmp(oldkey->GetName(), key->GetName())) continue;
        oldkey = key;

        // read object from source file
        source->cd(path.c_str());
        TObject *obj = key->ReadObj();

        if(obj->IsA()->InheritsFrom(TH1::Class()))
        {
            if(verbosity >= 4) 
            {
                printf("| Found TH1: %s\n", obj->GetName());
                fflush(stdout);
            }
            string path(target->GetPath());
            pair<string, string> okey(path.substr(path.find(':') + 2), obj->GetName());
            //cout << okey.first << "\t" << okey.second << endl;
            if(outputMap.find(okey) == outputMap.end())
            {
                outputVec.push_back(make_pair(path.substr(path.find(':') + 2), obj));
                outputMap[okey] = obj;
            }
            else
            {
                ((TH1*)outputMap[okey])->Add((TH1*)obj);
                ((TH1*)obj)->Delete();
            }
        }
        else if(obj->IsA()->InheritsFrom(TTree::Class()))
        {
            string path(target->GetPath());
            if(verbosity >= 4) 
            {
                printf("| Found Tree: %s\n", obj->GetName());
                fflush(stdout);
            }
            pair<string, string> okey(path.substr(path.find(':') + 2), obj->GetName());
            if(outputMap.find(okey) == outputMap.end())
            {
                string fname(okey.first);
                fname = fname + "_" + obj->GetName() + "tmpfile";
                for(size_t pos; (pos = fname.find('/')) != size_t(-1); ) fname[pos] = '_';
                tmpFiles.push_back(make_pair(fname, new TFile(fname.c_str(), "RECREATE")));
                TTree* tree = ((TTree*)obj)->CloneTree();
                ((TTree*)obj)->GetListOfClones()->Remove(tree);
                ((TTree*)obj)->ResetBranchAddresses();
                tree->ResetBranchAddresses();
                outputVec.push_back(make_pair(path.substr(path.find(':') + 2), (TObject*)tree));
                outputMap[okey] = (TObject*)tree;
            }
            else 
            {
                TTree* tm = (TTree*)outputMap[okey];
                TTree* ts = (TTree*)obj;
                tm->CopyAddresses(ts);
                for(int i = 0; i < ts->GetEntries(); i++)
                {
                    ts->GetEntry(i);
                    tm->Fill();
                }
                ts->ResetBranchAddresses();
                if (tm->GetTreeIndex()) tm->GetTreeIndex()->Append(ts->GetTreeIndex(), kTRUE); 
                ((TTree*)obj)->Delete();
            }
        }
        else if(obj->IsA()->InheritsFrom(TDirectory::Class()))
        {
            if(verbosity >= 3) 
            {
                printf("Hadding Directory: %s\n", ((TDirectory*)obj)->GetPath());
                fflush(stdout);
            }
            string path(((TDirectory*)obj)->GetPath());
            pair<string, string> okey(path.substr(path.find(':') + 2), " -------- ");
            if(outputMap.find(okey) == outputMap.end())
            {
                outputVec.push_back(make_pair(path.substr(path.find(':') + 2), (TDirectory*)0));
                outputMap[okey] = 0;
            }
            MergeRootfile(outputMap, outputVec, (TDirectory*)obj, source);
        }
        else
        {
            printf("Unknown object type, name: %s title: %s\n", obj->GetName(), obj->GetTitle());
            fflush(stdout);
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
        {"force",          no_argument, 0, 'f'},
        {"verbose",  required_argument, 0, 'v'},
    };

    while((opt = getopt_long(argn, argv, "fv:", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
            case 'f':
                isForce = true;
                break;
            case 'v':
                verbosity = int(atoi(optarg));
                break;
        }
    }

    if(argn - optind < 1)
    {
        printf("No target specified\n");
        exit(0);
    }
    string target(argv[optind]);
    if(verbosity >= 1)
    {
        printf("Target file: %s\n", target.c_str());
        fflush(stdout);
    }
    
    vector<string> sources;
    if(argn - optind < 2)
    {
        printf("No sources specified\n");
        exit(0);
    }
    for(int i = optind + 1; i < argn; i++)
    {
        sources.push_back(argv[i]);
        if(verbosity == 1)
        {
            printf("Source file: %s\n", sources.back().c_str());
            fflush(stdout);
        }
    }
    
    if(find(sources.begin(), sources.end(), target) != sources.end())
    {
        printf("Source list includes target file!!!\n");
        exit(0);
    }

    hadd(target, sources);
}
