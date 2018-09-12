using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public unsafe class ui : MonoBehaviour {

    public Button start;
    public Button move;
	public Button gen_sol;
    
	void Start () {
        start.onClick.AddListener (delegate { Llevel(); });
        move.onClick.AddListener (delegate { StartMove(); });
		gen_sol.onClick.AddListener (delegate {Gen_sol();});
	}

	void Gen_sol ()
	{
		// создаем из списка tbregiment список обычных reg_stat
		MakeReg_From_tbregiment_list();

		// Раскидать reg_stat по частям
		for (int i = 0; i < AI.Gen_stat.Count; i++) {
			AI.Gen_stat [i].MakeParts ();
		}
		// Вызвать построение внутри части
		for (int i = 0; i < AI.Gen_stat.Count; i++) {

			Debug.Log ("---------------------------------------------------------");
			for (int h = 0; h < AI.Gen_stat [i].parts.Count; h++) {
				AI.Gen_stat [i].parts [h].make_position ();
			}
		}

		/// Присваиваем конечные положения полкам
		for (int i = 0; i < AI.Gen_stat.Count; i++)
			for (int h = 0; h < AI.Gen_stat [i].parts.Count; h++) 
				for (int g = 0 ; g < AI.Gen_stat[i].parts[h].regiments.Count; g++)
			{
					int n = (* AI.Gen_stat [i].parts [h].regiments [g].thisregiment).number;
					Vector3 pos = tbdata.tablregiment[n].gameObject.transform.position;
					pos.y += 1;
					Vector3 posreg = new Vector3 (AI.Gen_stat [i].parts [h].regiments [g].new_pos.x, 1 + AI.Gen_stat [i].parts [h].regiments [g].tablereg.regPosition.y,
					AI.Gen_stat [i].parts [h].regiments [g].new_pos.y);

					// Если уже есть - поменять место.
					if ( tbdata.tablregiment[n].newPos != null )
					{
						tbdata.tablregiment[n].newPos.transform.position = new Vector3(pos.x, pos.y, pos.z);
						tbdata.tablregiment[n].line.GetComponent<LineRenderer>().SetPosition(1, new Vector3(posreg.x, posreg.y, posreg.z) );
					}

					// Если нет, то создаем
					if (tbdata.tablregiment [n].newPos == null) 
					{
						tbdata.tablregiment[n].newPos = Instantiate (tbdata.tablregiment[n].Pref, new Vector3(posreg.x, posreg.y, posreg.z), Quaternion.identity);
						tbdata.tablregiment[n].newPos.GetComponent<newRegPos> ().num = tbdata.tablregiment[n].number;
						//tbdata.tablregiment[n].bag = true;
						tbdata.tablregiment[n].line = Instantiate (tbdata.tablregiment[n].linePref, new Vector3 (0, 0.1f, 0), Quaternion.identity);
						tbdata.tablregiment[n].line.GetComponent<LineRenderer> ().SetPosition (0, new Vector3(pos.x, pos.y, pos.z));
						tbdata.tablregiment[n].line.GetComponent<LineRenderer> ().SetPosition (1, new Vector3(posreg.x, posreg.y, posreg.z));
					}
			}	
	}

    void Llevel()
    {
		 tbsave.MakeSave();
         Application.LoadLevel(1);  
	}

    void StartMove()
    {
		tbstatus.mov = true;
    }

	void MakeReg_From_tbregiment_list ()
	{
		for (int i = 0; i < tbdata.tablregiment.Count; i++) {
			reg_stat temp = new reg_stat() ;
			temp.tablereg = new regStat ();
			temp.tablereg.angle = tbdata.tablregiment [i].angle;
			temp.tablereg.command = tbdata.tablregiment [i].command;
			temp.tablereg.prim_soldier_count = tbdata.tablregiment [i].countsol;
			temp.tablereg.regFirstLine = tbdata.tablregiment [i].fline;
			temp.tablereg.regDistanseLine = tbdata.tablregiment [i].fline_dist;
			temp.tablereg.number = tbdata.tablregiment [i].number;
			temp.tablereg.regPosition = tbdata.tablregiment [i].position;
			AI.Gen_stat [temp.tablereg.command].tempstats.Add (temp);
		}
	}    
}
